/***
*
*	Copyright (c) 1996-2001, Valve LLC. All rights reserved.
*
*	This product contains software technology licensed from Id
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc.
*	All Rights Reserved.
*
*   This source code contains proprietary and confidential information of
*   Valve LLC and its suppliers.  Access to this code is restricted to
*   persons who have executed a written SDK license with Valve.  Any access,
*   use or distribution of this code by or to any unlicensed person is illegal.
*
****/
//=========================================================
// Houndeye - spooky sonic dog. 
//=========================================================

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"monsters.h"
#include	"schedule.h"
#include	"animation.h"
#include	"nodes.h"
#include	"squadmonster.h"
#include	"soundent.h"
#include	"game.h"

extern CGraph WorldGraph;

//=========================================================
// Constants
//=========================================================
constexpr int HOUNDEYE_MAX_SQUAD_SIZE = 4;
constexpr int HOUNDEYE_MAX_ATTACK_RADIUS = 384;
constexpr float HOUNDEYE_SQUAD_BONUS = 1.1f;
constexpr int HOUNDEYE_EYE_FRAMES = 4;
constexpr int HOUNDEYE_SOUND_STARTLE_VOLUME = 128;
constexpr float HOUNDEYE_FOLLOW_RANGE = 128.0f;

//=========================================================
// Monster's Anim Events Go Here
//=========================================================
enum HoundeyeAnimEvent
{
	HOUND_AE_WARN = 1,
	HOUND_AE_STARTATTACK = 2,
	HOUND_AE_THUMP = 3,
	HOUND_AE_ANGERSOUND1 = 4,
	HOUND_AE_ANGERSOUND2 = 5,
	HOUND_AE_HOPBACK = 6,
	HOUND_AE_CLOSE_EYE = 7
};

//=========================================================
// monster-specific tasks
//=========================================================
enum HoundeyeTask
{
	TASK_HOUND_CLOSE_EYE = LAST_COMMON_TASK + 1,
	TASK_HOUND_OPEN_EYE,
	TASK_HOUND_THREAT_DISPLAY,
	TASK_HOUND_FALL_ASLEEP,
	TASK_HOUND_WAKE_UP,
	TASK_HOUND_HOP_BACK
};

//=========================================================
// monster-specific schedule types
//=========================================================
enum HoundeyeSchedule
{
	SCHED_HOUND_AGITATED = LAST_COMMON_SCHEDULE + 1,
	SCHED_HOUND_HOP_RETREAT,
	SCHED_HOUND_FAIL,
};

class CHoundeye : public CSquadMonster
{
public:
	void Spawn(void) override;
	void Precache(void) override;
	int  Classify(void) override;
	void HandleAnimEvent(MonsterEvent_t* pEvent) override;
	void SetYawSpeed(void) override;
	void WarmUpSound(void);
	void AlertSound(void) override;
	void DeathSound(void) override;
	void WarnSound(void);
	void PainSound(void) override;
	void IdleSound(void) override;
	void StartTask(Task_t* pTask) override;
	void RunTask(Task_t* pTask) override;
	void SonicAttack(void);
	void PrescheduleThink(void) override;
	void SetActivity(Activity NewActivity) override;
	void WriteBeamColor(void);
	BOOL CheckRangeAttack1(float flDot, float flDist) override;
	BOOL FValidateHintType(short sHint) override;
	BOOL FCanActiveIdle(void) override;
	Schedule_t* GetScheduleOfType(int Type) override;
	Schedule_t* GetSchedule(void) override;

	void KeyValue(KeyValueData* pkvd) override;
	void DeclineFollowing(void);
	void FollowerUse(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value);
	void StartFollowing(CBaseEntity* pLeader);
	BOOL IsFollowing(void);
	void StopFollowing(BOOL bClearSchedule);

	int TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType) override;

	int ObjectCaps() override
	{
		if (m_bFriendly)
			return CSquadMonster::ObjectCaps() | FCAP_IMPULSE_USE;
		return CSquadMonster::ObjectCaps();
	}

	int	Save(CSave& save) override;
	int Restore(CRestore& restore) override;

	CUSTOM_SCHEDULES;
	static TYPEDESCRIPTION m_SaveData[];

	int m_iSpriteTexture;
	BOOL m_fAsleep;          // some houndeyes sleep in idle mode if this is set, the houndeye is lying down
	BOOL m_fDontBlink;       // don't try to open/close eye if this bit is set!
	Vector m_vecPackCenter;  // the center of the pack. The leader maintains this by averaging the origins of all pack members.

	BOOL m_bFriendly;        // If TRUE, Houndeye is friendly to player and player allies
	float m_flPlayerDamage;  // How much pain has the player inflicted on me?
};

LINK_ENTITY_TO_CLASS(monster_houndeye, CHoundeye);

TYPEDESCRIPTION CHoundeye::m_SaveData[] =
{
	DEFINE_FIELD(CHoundeye, m_iSpriteTexture, FIELD_INTEGER),
	DEFINE_FIELD(CHoundeye, m_fAsleep, FIELD_BOOLEAN),
	DEFINE_FIELD(CHoundeye, m_fDontBlink, FIELD_BOOLEAN),
	DEFINE_FIELD(CHoundeye, m_vecPackCenter, FIELD_POSITION_VECTOR),
	DEFINE_FIELD(CHoundeye, m_bFriendly, FIELD_BOOLEAN),
	DEFINE_FIELD(CHoundeye, m_flPlayerDamage, FIELD_FLOAT),
	DEFINE_FIELD(CHoundeye, m_hTargetEnt, FIELD_EHANDLE),
};

IMPLEMENT_SAVERESTORE(CHoundeye, CSquadMonster);

//=========================================================
// KeyValue - handles custom entity properties
//=========================================================
void CHoundeye::KeyValue(KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "friend_to_player"))
	{
		m_bFriendly = static_cast<BOOL>(atoi(pkvd->szValue));
		pkvd->fHandled = TRUE;
	}
	else
	{
		CSquadMonster::KeyValue(pkvd);
	}
}

//=========================================================
// Classify - indicates this monster's place in the 
// relationship table.
//=========================================================
int CHoundeye::Classify(void)
{
	if (m_bFriendly)
		return CLASS_PLAYER_ALLY;

	return CLASS_ALIEN_MONSTER;
}

//=========================================================
//  FValidateHintType 
//=========================================================
BOOL CHoundeye::FValidateHintType(short sHint)
{
	static short sHoundHints[] =
	{
		HINT_WORLD_MACHINERY,
		HINT_WORLD_BLINKING_LIGHT,
		HINT_WORLD_HUMAN_BLOOD,
		HINT_WORLD_ALIEN_BLOOD,
	};

	for (int i = 0; i < ARRAYSIZE(sHoundHints); ++i)
	{
		if (sHoundHints[i] == sHint)
		{
			return TRUE;
		}
	}

	ALERT(at_aiconsole, "Couldn't validate hint type");
	return FALSE;
}

//=========================================================
// FCanActiveIdle
//=========================================================
BOOL CHoundeye::FCanActiveIdle(void)
{
	if (InSquad())
	{
		CSquadMonster* pSquadLeader = MySquadLeader();

		for (int i = 0; i < MAX_SQUAD_MEMBERS; ++i)
		{
			CSquadMonster* pMember = pSquadLeader->MySquadMember(i);

			if (pMember != nullptr && pMember != this && pMember->m_iHintNode != NO_NODE)
			{
				// someone else in the group is active idling right now!
				return FALSE;
			}
		}

		return TRUE;
	}

	return TRUE;
}

//=========================================================
// CheckRangeAttack1 - overridden for houndeyes so that they
// try to get within half of their max attack radius before
// attacking, so as to increase their chances of doing damage.
//=========================================================
BOOL CHoundeye::CheckRangeAttack1(float flDot, float flDist)
{
	// Friendly houndeyes won't attack players or allies
	if (m_bFriendly && m_hEnemy != nullptr)
	{
		if (m_hEnemy->IsPlayer())
			return FALSE;
		if (m_hEnemy->Classify() == CLASS_PLAYER_ALLY)
			return FALSE;
	}

	if (flDist <= (HOUNDEYE_MAX_ATTACK_RADIUS * 0.5f) && flDot >= 0.3f)
	{
		return TRUE;
	}
	return FALSE;
}

//=========================================================
// SetYawSpeed - allows each sequence to have a different
// turn rate associated with it.
//=========================================================
void CHoundeye::SetYawSpeed(void)
{
	int ys = 90;

	switch (m_Activity)
	{
	case ACT_CROUCHIDLE: // sleeping!
		ys = 0;
		break;
	case ACT_IDLE:
		ys = 60;
		break;
	case ACT_WALK:
		ys = 90;
		break;
	case ACT_RUN:
		ys = 90;
		break;
	case ACT_TURN_LEFT:
	case ACT_TURN_RIGHT:
		ys = 90;
		break;
	default:
		break;
	}

	pev->yaw_speed = static_cast<float>(ys);
}

//=========================================================
// SetActivity 
//=========================================================
void CHoundeye::SetActivity(Activity NewActivity)
{
	int iSequence;

	if (NewActivity == m_Activity)
		return;

	if (m_MonsterState == MONSTERSTATE_COMBAT && NewActivity == ACT_IDLE && RANDOM_LONG(0, 1))
	{
		// play pissed idle.
		iSequence = LookupSequence("madidle");

		m_Activity = NewActivity; // Go ahead and set this so it doesn't keep trying when the anim is not present

		// In case someone calls this with something other than the ideal activity
		m_IdealActivity = m_Activity;

		// Set to the desired anim, or default anim if the desired is not present
		if (iSequence > ACTIVITY_NOT_AVAILABLE)
		{
			pev->sequence = iSequence; // Set to the reset anim (if it's there)
			pev->frame = 0;            // FIX: frame counter shouldn't be reset when its the same activity as before
			ResetSequenceInfo();
			SetYawSpeed();
		}
	}
	else
	{
		CSquadMonster::SetActivity(NewActivity);
	}
}

//=========================================================
// HandleAnimEvent - catches the monster-specific messages
// that occur when tagged animation frames are played.
//=========================================================
void CHoundeye::HandleAnimEvent(MonsterEvent_t* pEvent)
{
	switch (pEvent->event)
	{
	case HOUND_AE_WARN:
		WarnSound();
		break;

	case HOUND_AE_STARTATTACK:
		WarmUpSound();
		break;

	case HOUND_AE_HOPBACK:
	{
		float flGravity = g_psv_gravity->value;

		pev->flags &= ~FL_ONGROUND;

		pev->velocity = gpGlobals->v_forward * -200.0f;
		pev->velocity.z += (0.6f * flGravity) * 0.5f;
		break;
	}

	case HOUND_AE_THUMP:
		SonicAttack();
		break;

	case HOUND_AE_ANGERSOUND1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_pain3.wav", 1.0f, ATTN_NORM);
		break;

	case HOUND_AE_ANGERSOUND2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_pain1.wav", 1.0f, ATTN_NORM);
		break;

	case HOUND_AE_CLOSE_EYE:
		if (!m_fDontBlink)
		{
			pev->skin = HOUNDEYE_EYE_FRAMES - 1;
		}
		break;

	default:
		CSquadMonster::HandleAnimEvent(pEvent);
		break;
	}
}

//=========================================================
// Spawn
//=========================================================
void CHoundeye::Spawn()
{
	Precache();

	m_flPlayerDamage = 0.0f;

	SET_MODEL(ENT(pev), "models/houndeye.mdl");
	UTIL_SetSize(pev, Vector(-16.0f, -16.0f, 0.0f), Vector(16.0f, 16.0f, 36.0f));

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_YELLOW;
	pev->effects = 0;
	pev->health = gSkillData.houndeyeHealth;
	pev->yaw_speed = 5.0f; //!!! should we put this in the monster's changeanim function since turn rates may vary with state/anim?
	m_flFieldOfView = 0.5f; // indicates the width of this monster's forward view cone ( as a dotproduct result )
	m_MonsterState = MONSTERSTATE_NONE;
	m_fAsleep = FALSE;       // everyone spawns awake
	m_fDontBlink = FALSE;
	m_afCapability |= bits_CAP_SQUAD;

	MonsterInit();

	// Only allow following if Houndeye is friendly
	if (m_bFriendly)
	{
		SetUse(&CHoundeye::FollowerUse);
	}
	else
	{
		SetUse(nullptr);
	}
}

//=========================================================
// Precache - precaches all resources this monster needs
//=========================================================
void CHoundeye::Precache()
{
	PRECACHE_MODEL("models/houndeye.mdl");

	PRECACHE_SOUND("houndeye/he_alert1.wav");
	PRECACHE_SOUND("houndeye/he_alert2.wav");
	PRECACHE_SOUND("houndeye/he_alert3.wav");

	PRECACHE_SOUND("houndeye/he_die1.wav");
	PRECACHE_SOUND("houndeye/he_die2.wav");
	PRECACHE_SOUND("houndeye/he_die3.wav");

	PRECACHE_SOUND("houndeye/he_idle1.wav");
	PRECACHE_SOUND("houndeye/he_idle2.wav");
	PRECACHE_SOUND("houndeye/he_idle3.wav");

	PRECACHE_SOUND("houndeye/he_hunt1.wav");
	PRECACHE_SOUND("houndeye/he_hunt2.wav");
	PRECACHE_SOUND("houndeye/he_hunt3.wav");

	PRECACHE_SOUND("houndeye/he_pain1.wav");
	PRECACHE_SOUND("houndeye/he_pain3.wav");
	PRECACHE_SOUND("houndeye/he_pain4.wav");
	PRECACHE_SOUND("houndeye/he_pain5.wav");

	PRECACHE_SOUND("houndeye/he_attack1.wav");
	PRECACHE_SOUND("houndeye/he_attack3.wav");

	PRECACHE_SOUND("houndeye/he_blast1.wav");
	PRECACHE_SOUND("houndeye/he_blast2.wav");
	PRECACHE_SOUND("houndeye/he_blast3.wav");

	m_iSpriteTexture = PRECACHE_MODEL("sprites/shockwave.spr");
}

//=========================================================
// IdleSound
//=========================================================
void CHoundeye::IdleSound(void)
{
	switch (RANDOM_LONG(0, 2))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_idle1.wav", 1.0f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_idle2.wav", 1.0f, ATTN_NORM);
		break;
	case 2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_idle3.wav", 1.0f, ATTN_NORM);
		break;
	}
}

//=========================================================
// WarmUpSound
//=========================================================
void CHoundeye::WarmUpSound(void)
{
	switch (RANDOM_LONG(0, 1))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "houndeye/he_attack1.wav", 0.7f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "houndeye/he_attack3.wav", 0.7f, ATTN_NORM);
		break;
	}
}

//=========================================================
// WarnSound 
//=========================================================
void CHoundeye::WarnSound(void)
{
	switch (RANDOM_LONG(0, 2))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_hunt1.wav", 1.0f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_hunt2.wav", 1.0f, ATTN_NORM);
		break;
	case 2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_hunt3.wav", 1.0f, ATTN_NORM);
		break;
	}
}

//=========================================================
// AlertSound 
//=========================================================
void CHoundeye::AlertSound(void)
{
	if (InSquad() && !IsLeader())
	{
		return; // only leader makes ALERT sound.
	}

	switch (RANDOM_LONG(0, 2))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_alert1.wav", 1.0f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_alert2.wav", 1.0f, ATTN_NORM);
		break;
	case 2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_alert3.wav", 1.0f, ATTN_NORM);
		break;
	}
}

//=========================================================
// DeathSound 
//=========================================================
void CHoundeye::DeathSound(void)
{
	switch (RANDOM_LONG(0, 2))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_die1.wav", 1.0f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_die2.wav", 1.0f, ATTN_NORM);
		break;
	case 2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_die3.wav", 1.0f, ATTN_NORM);
		break;
	}
}

//=========================================================
// PainSound 
//=========================================================
void CHoundeye::PainSound(void)
{
	switch (RANDOM_LONG(0, 2))
	{
	case 0:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_pain3.wav", 1.0f, ATTN_NORM);
		break;
	case 1:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_pain4.wav", 1.0f, ATTN_NORM);
		break;
	case 2:
		EMIT_SOUND(ENT(pev), CHAN_VOICE, "houndeye/he_pain5.wav", 1.0f, ATTN_NORM);
		break;
	}
}

//=========================================================
// TakeDamage - get provoked when injured
//=========================================================
int CHoundeye::TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType)
{
	CBaseEntity* pAttacker = CBaseEntity::Instance(pevAttacker);

	// If friendly and attacked by player, become hostile
	if (m_bFriendly && pAttacker && pAttacker->IsPlayer())
	{
		m_flPlayerDamage += flDamage;
		m_afMemory |= bits_MEMORY_PROVOKED;

		// Turn hostile to player
		m_bFriendly = FALSE;
		m_hEnemy = pAttacker;
		SetUse(nullptr);

		// Play pain sound
		PainSound();

		// Force state to combat
		SetState(MONSTERSTATE_COMBAT);

		// Force schedule change to combat
		ChangeSchedule(GetScheduleOfType(SCHED_RANGE_ATTACK1));

		return CSquadMonster::TakeDamage(pevInflictor, pevAttacker, flDamage, bitsDamageType);
	}

	m_afMemory |= bits_MEMORY_PROVOKED;
	return CSquadMonster::TakeDamage(pevInflictor, pevAttacker, flDamage, bitsDamageType);
}

//=========================================================
// WriteBeamColor - writes a color vector to the network 
// based on the size of the group. 
//=========================================================
void CHoundeye::WriteBeamColor(void)
{
	BYTE bRed, bGreen, bBlue;

	if (InSquad())
	{
		switch (SquadCount())
		{
		case 2:
			// no case for 0 or 1, cause those are impossible for monsters in Squads.
			bRed = 101;
			bGreen = 133;
			bBlue = 221;
			break;
		case 3:
			bRed = 67;
			bGreen = 85;
			bBlue = 255;
			break;
		case 4:
			bRed = 62;
			bGreen = 33;
			bBlue = 211;
			break;
		default:
			ALERT(at_aiconsole, "Unsupported Houndeye SquadSize!\n");
			bRed = 188;
			bGreen = 220;
			bBlue = 255;
			break;
		}
	}
	else
	{
		// solo houndeye - weakest beam
		bRed = 188;
		bGreen = 220;
		bBlue = 255;
	}

	WRITE_BYTE(bRed);
	WRITE_BYTE(bGreen);
	WRITE_BYTE(bBlue);
}

//=========================================================
// SonicAttack
//=========================================================
void CHoundeye::SonicAttack(void)
{
	float flAdjustedDamage;
	float flDist;

	switch (RANDOM_LONG(0, 2))
	{
	case 0: EMIT_SOUND(ENT(pev), CHAN_WEAPON, "houndeye/he_blast1.wav", 1.0f, ATTN_NORM); break;
	case 1: EMIT_SOUND(ENT(pev), CHAN_WEAPON, "houndeye/he_blast2.wav", 1.0f, ATTN_NORM); break;
	case 2: EMIT_SOUND(ENT(pev), CHAN_WEAPON, "houndeye/he_blast3.wav", 1.0f, ATTN_NORM); break;
	}

	// blast circles
	MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pev->origin);
	WRITE_BYTE(TE_BEAMCYLINDER);
	WRITE_COORD(pev->origin.x);
	WRITE_COORD(pev->origin.y);
	WRITE_COORD(pev->origin.z + 16.0f);
	WRITE_COORD(pev->origin.x);
	WRITE_COORD(pev->origin.y);
	WRITE_COORD(pev->origin.z + 16.0f + static_cast<float>(HOUNDEYE_MAX_ATTACK_RADIUS) / 0.2f); // reach damage radius over .3 seconds
	WRITE_SHORT(m_iSpriteTexture);
	WRITE_BYTE(0);   // startframe
	WRITE_BYTE(0);   // framerate
	WRITE_BYTE(2);   // life
	WRITE_BYTE(16);  // width
	WRITE_BYTE(0);   // noise

	WriteBeamColor();

	WRITE_BYTE(255); // brightness
	WRITE_BYTE(0);   // speed
	MESSAGE_END();

	MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pev->origin);
	WRITE_BYTE(TE_BEAMCYLINDER);
	WRITE_COORD(pev->origin.x);
	WRITE_COORD(pev->origin.y);
	WRITE_COORD(pev->origin.z + 16.0f);
	WRITE_COORD(pev->origin.x);
	WRITE_COORD(pev->origin.y);
	WRITE_COORD(pev->origin.z + 16.0f + (static_cast<float>(HOUNDEYE_MAX_ATTACK_RADIUS) / 2.0f) / 0.2f); // reach damage radius over .3 seconds
	WRITE_SHORT(m_iSpriteTexture);
	WRITE_BYTE(0);   // startframe
	WRITE_BYTE(0);   // framerate
	WRITE_BYTE(2);   // life
	WRITE_BYTE(16);  // width
	WRITE_BYTE(0);   // noise

	WriteBeamColor();

	WRITE_BYTE(255); // brightness
	WRITE_BYTE(0);   // speed
	MESSAGE_END();

	CBaseEntity* pEntity = nullptr;
	// iterate on all entities in the vicinity.
	while ((pEntity = UTIL_FindEntityInSphere(pEntity, pev->origin, HOUNDEYE_MAX_ATTACK_RADIUS)) != nullptr)
	{
		if (pEntity->pev->takedamage != DAMAGE_NO)
		{
			if (m_bFriendly)
			{
				// Don't hurt players
				if (pEntity->IsPlayer())
					continue;

				// Don't hurt player allies
				if (pEntity->Classify() == CLASS_PLAYER_ALLY)
					continue;
			}

			// Friendly houndeyes don't hurt other friendly houndeyes
			if (FClassnameIs(pEntity->pev, "monster_houndeye"))
			{
				CHoundeye* pHound = static_cast<CHoundeye*>(pEntity);
				if (pHound && pHound->m_bFriendly == m_bFriendly)
				{
					continue; // Don't hurt same-faction houndeyes
				}
			}

			// houndeyes do FULL damage if the ent in question is visible. Half damage otherwise.
			// This means that you must get out of the houndeye's attack range entirely to avoid damage.
			// Calculate full damage first

			if (SquadCount() > 1)
			{
				// squad gets attack bonus.
				flAdjustedDamage = gSkillData.houndeyeDmgBlast + gSkillData.houndeyeDmgBlast * (HOUNDEYE_SQUAD_BONUS * static_cast<float>(SquadCount() - 1));
			}
			else
			{
				// solo
				flAdjustedDamage = static_cast<float>(gSkillData.houndeyeDmgBlast);
			}

			flDist = (pEntity->Center() - pev->origin).Length();

			flAdjustedDamage -= (flDist / static_cast<float>(HOUNDEYE_MAX_ATTACK_RADIUS)) * flAdjustedDamage;

			if (!FVisible(pEntity))
			{
				if (pEntity->IsPlayer())
				{
					// if this entity is a client, and is not in full view, inflict half damage. We do this so that players still 
					// take the residual damage if they don't totally leave the houndeye's effective radius. We restrict it to clients
					// so that monsters in other parts of the level don't take the damage and get pissed.
					flAdjustedDamage *= 0.5f;
				}
				else if (!FClassnameIs(pEntity->pev, "func_breakable") && !FClassnameIs(pEntity->pev, "func_pushable"))
				{
					// do not hurt nonclients through walls, but allow damage to be done to breakables
					flAdjustedDamage = 0.0f;
				}
			}

			if (flAdjustedDamage > 0.0f)
			{
				pEntity->TakeDamage(pev, pev, flAdjustedDamage, DMG_SONIC | DMG_ALWAYSGIB);
			}
		}
	}
}

//=========================================================
// start task
//=========================================================
void CHoundeye::StartTask(Task_t* pTask)
{
	m_iTaskStatus = TASKSTATUS_RUNNING;

	switch (pTask->iTask)
	{
	case TASK_HOUND_FALL_ASLEEP:
	{
		m_fAsleep = TRUE; // signal that hound is lying down (must stand again before doing anything else!)
		m_iTaskStatus = TASKSTATUS_COMPLETE;
		break;
	}
	case TASK_HOUND_WAKE_UP:
	{
		m_fAsleep = FALSE; // signal that hound is standing again
		m_iTaskStatus = TASKSTATUS_COMPLETE;
		break;
	}
	case TASK_HOUND_OPEN_EYE:
	{
		m_fDontBlink = FALSE; // turn blinking back on and that code will automatically open the eye
		m_iTaskStatus = TASKSTATUS_COMPLETE;
		break;
	}
	case TASK_HOUND_CLOSE_EYE:
	{
		pev->skin = 0;
		m_fDontBlink = TRUE; // tell blink code to leave the eye alone.
		break;
	}
	case TASK_HOUND_THREAT_DISPLAY:
	{
		m_IdealActivity = ACT_IDLE_ANGRY;
		break;
	}
	case TASK_HOUND_HOP_BACK:
	{
		m_IdealActivity = ACT_LEAP;
		break;
	}
	case TASK_RANGE_ATTACK1:
	{
		m_IdealActivity = ACT_RANGE_ATTACK1;
		break;
	}
	case TASK_SPECIAL_ATTACK1:
	{
		m_IdealActivity = ACT_SPECIAL_ATTACK1;
		break;
	}
	case TASK_GUARD:
	{
		m_IdealActivity = ACT_GUARD;
		break;
	}
	default:
	{
		CSquadMonster::StartTask(pTask);
		break;
	}
	}
}

//=========================================================
// RunTask 
//=========================================================
void CHoundeye::RunTask(Task_t* pTask)
{
	switch (pTask->iTask)
	{
	case TASK_HOUND_THREAT_DISPLAY:
	{
		MakeIdealYaw(m_vecEnemyLKP);
		ChangeYaw(pev->yaw_speed);

		if (m_fSequenceFinished)
		{
			TaskComplete();
		}
		break;
	}
	case TASK_HOUND_CLOSE_EYE:
	{
		if (pev->skin < HOUNDEYE_EYE_FRAMES - 1)
		{
			pev->skin++;
		}
		break;
	}
	case TASK_HOUND_HOP_BACK:
	{
		if (m_fSequenceFinished)
		{
			TaskComplete();
		}
		break;
	}
	case TASK_SPECIAL_ATTACK1:
	{
		pev->skin = RANDOM_LONG(0, HOUNDEYE_EYE_FRAMES - 1);

		MakeIdealYaw(m_vecEnemyLKP);
		ChangeYaw(pev->yaw_speed);

		float life;
		life = ((255.0f - pev->frame) / (pev->framerate * m_flFrameRate));
		if (life < 0.1f) life = 0.1f;

		MESSAGE_BEGIN(MSG_PAS, SVC_TEMPENTITY, pev->origin);
		WRITE_BYTE(TE_IMPLOSION);
		WRITE_COORD(pev->origin.x);
		WRITE_COORD(pev->origin.y);
		WRITE_COORD(pev->origin.z + 16.0f);
		WRITE_BYTE(static_cast<int>(50.0f * life + 100.0f)); // radius
		WRITE_BYTE(static_cast<int>(pev->frame / 25.0f));    // count
		WRITE_BYTE(static_cast<int>(life * 10.0f));          // life
		MESSAGE_END();

		if (m_fSequenceFinished)
		{
			SonicAttack();
			TaskComplete();
		}
		break;
	}
	default:
	{
		CSquadMonster::RunTask(pTask);
		break;
	}
	}
}

//=========================================================
// PrescheduleThink
//=========================================================
void CHoundeye::PrescheduleThink(void)
{
	// if the hound is mad and is running, make hunt noises.
	if (m_MonsterState == MONSTERSTATE_COMBAT && m_Activity == ACT_RUN && RANDOM_FLOAT(0.0f, 1.0f) < 0.2f)
	{
		WarnSound();
	}

	// at random, initiate a blink if not already blinking or sleeping
	if (!m_fDontBlink)
	{
		if ((pev->skin == 0) && RANDOM_LONG(0, 0x7F) == 0)
		{// start blinking!
			pev->skin = HOUNDEYE_EYE_FRAMES - 1;
		}
		else if (pev->skin != 0)
		{// already blinking
			pev->skin--;
		}
	}

	// if you are the leader, average the origins of each pack member to get an approximate center.
	if (IsLeader())
	{
		CSquadMonster* pSquadMember;
		int iSquadCount = 0;

		m_vecPackCenter = Vector(0.0f, 0.0f, 0.0f);

		for (int i = 0; i < MAX_SQUAD_MEMBERS; ++i)
		{
			pSquadMember = MySquadMember(i);

			if (pSquadMember)
			{
				++iSquadCount;
				m_vecPackCenter = m_vecPackCenter + pSquadMember->pev->origin;
			}
		}

		if (iSquadCount > 0)
		{
			m_vecPackCenter = m_vecPackCenter / static_cast<float>(iSquadCount);
		}
	}
}

//=========================================================
// AI Schedules Specific to this monster
//=========================================================
Task_t tlHoundGuardPack[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_GUARD, 0.0f },
};

Schedule_t slHoundGuardPack[] =
{
	{
		tlHoundGuardPack,
		ARRAYSIZE(tlHoundGuardPack),
		bits_COND_SEE_HATE |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_PROVOKED |
		bits_COND_HEAR_SOUND,
		bits_SOUND_COMBAT | // sound flags
		bits_SOUND_WORLD |
		bits_SOUND_MEAT |
		bits_SOUND_PLAYER,
		"GuardPack"
	},
};

// primary range attack
Task_t tlHoundYell1[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_IDEAL, 0.0f },
	{ TASK_RANGE_ATTACK1, 0.0f },
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_HOUND_AGITATED) },
};

Task_t tlHoundYell2[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_IDEAL, 0.0f },
	{ TASK_RANGE_ATTACK1, 0.0f },
};

Schedule_t slHoundRangeAttack[] =
{
	{
		tlHoundYell1,
		ARRAYSIZE(tlHoundYell1),
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		0,
		"HoundRangeAttack1"
	},
	{
		tlHoundYell2,
		ARRAYSIZE(tlHoundYell2),
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		0,
		"HoundRangeAttack2"
	},
};

// lie down and fall asleep
Task_t tlHoundSleep[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_WAIT_RANDOM, 5.0f },
	{ TASK_PLAY_SEQUENCE, static_cast<float>(ACT_CROUCH) },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_CROUCHIDLE) },
	{ TASK_HOUND_FALL_ASLEEP, 0.0f },
	{ TASK_WAIT_RANDOM, 25.0f },
	{ TASK_HOUND_CLOSE_EYE, 0.0f },
};

Schedule_t slHoundSleep[] =
{
	{
		tlHoundSleep,
		ARRAYSIZE(tlHoundSleep),
		bits_COND_HEAR_SOUND |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_NEW_ENEMY,
		bits_SOUND_COMBAT |
		bits_SOUND_PLAYER |
		bits_SOUND_WORLD,
		"Hound Sleep"
	},
};

// wake and stand up lazily
Task_t tlHoundWakeLazy[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_HOUND_OPEN_EYE, 0.0f },
	{ TASK_WAIT_RANDOM, 2.5f },
	{ TASK_PLAY_SEQUENCE, static_cast<float>(ACT_STAND) },
	{ TASK_HOUND_WAKE_UP, 0.0f },
};

Schedule_t slHoundWakeLazy[] =
{
	{
		tlHoundWakeLazy,
		ARRAYSIZE(tlHoundWakeLazy),
		0,
		0,
		"WakeLazy"
	},
};

// wake and stand up with great urgency!
Task_t tlHoundWakeUrgent[] =
{
	{ TASK_HOUND_OPEN_EYE, 0.0f },
	{ TASK_PLAY_SEQUENCE, static_cast<float>(ACT_HOP) },
	{ TASK_FACE_IDEAL, 0.0f },
	{ TASK_HOUND_WAKE_UP, 0.0f },
};

Schedule_t slHoundWakeUrgent[] =
{
	{
		tlHoundWakeUrgent,
		ARRAYSIZE(tlHoundWakeUrgent),
		0,
		0,
		"WakeUrgent"
	},
};

Task_t tlHoundSpecialAttack1[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_FACE_IDEAL, 0.0f },
	{ TASK_SPECIAL_ATTACK1, 0.0f },
	{ TASK_PLAY_SEQUENCE, static_cast<float>(ACT_IDLE_ANGRY) },
};

Schedule_t slHoundSpecialAttack1[] =
{
	{
		tlHoundSpecialAttack1,
		ARRAYSIZE(tlHoundSpecialAttack1),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_ENEMY_OCCLUDED,
		0,
		"Hound Special Attack1"
	},
};

Task_t tlHoundAgitated[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_HOUND_THREAT_DISPLAY, 0.0f },
};

Schedule_t slHoundAgitated[] =
{
	{
		tlHoundAgitated,
		ARRAYSIZE(tlHoundAgitated),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		0,
		"Hound Agitated"
	},
};

Task_t tlHoundHopRetreat[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_HOUND_HOP_BACK, 0.0f },
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_TAKE_COVER_FROM_ENEMY) },
};

Schedule_t slHoundHopRetreat[] =
{
	{
		tlHoundHopRetreat,
		ARRAYSIZE(tlHoundHopRetreat),
		0,
		0,
		"Hound Hop Retreat"
	},
};

// hound fails in combat with client in the PVS
Task_t tlHoundCombatFailPVS[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_HOUND_THREAT_DISPLAY, 0.0f },
	{ TASK_WAIT_FACE_ENEMY, 1.0f },
};

Schedule_t slHoundCombatFailPVS[] =
{
	{
		tlHoundCombatFailPVS,
		ARRAYSIZE(tlHoundCombatFailPVS),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		0,
		"HoundCombatFailPVS"
	},
};

// hound fails in combat with no client in the PVS. Don't keep peeping!
Task_t tlHoundCombatFailNoPVS[] =
{
	{ TASK_STOP_MOVING, 0.0f },
	{ TASK_HOUND_THREAT_DISPLAY, 0.0f },
	{ TASK_WAIT_FACE_ENEMY, 2.0f },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_WAIT_PVS, 0.0f },
};

Schedule_t slHoundCombatFailNoPVS[] =
{
	{
		tlHoundCombatFailNoPVS,
		ARRAYSIZE(tlHoundCombatFailNoPVS),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE,
		0,
		"HoundCombatFailNoPVS"
	},
};

//=========================================================
// Follow schedule - Houndeye follows the player
//=========================================================
Task_t tlHoundFollow[] =
{
	{ TASK_MOVE_TO_TARGET_RANGE, static_cast<float>(HOUNDEYE_FOLLOW_RANGE) },
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_TARGET_FACE) },
};

Schedule_t slHoundFollow[] =
{
	{
		tlHoundFollow,
		ARRAYSIZE(tlHoundFollow),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_PROVOKED,
		bits_SOUND_DANGER,
		"HoundFollow"
	},
};

//=========================================================
// Face target schedule - Houndeye faces the player
//=========================================================
Task_t tlHoundFaceTarget[] =
{
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_FACE_TARGET, 0.0f },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_TARGET_CHASE) },
};

Schedule_t slHoundFaceTarget[] =
{
	{
		tlHoundFaceTarget,
		ARRAYSIZE(tlHoundFaceTarget),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_PROVOKED,
		bits_SOUND_DANGER,
		"HoundFaceTarget"
	},
};

DEFINE_CUSTOM_SCHEDULES(CHoundeye)
{
	slHoundGuardPack,
		slHoundRangeAttack,
		& slHoundRangeAttack[1],
		slHoundSleep,
		slHoundWakeLazy,
		slHoundWakeUrgent,
		slHoundSpecialAttack1,
		slHoundAgitated,
		slHoundHopRetreat,
		slHoundCombatFailPVS,
		slHoundCombatFailNoPVS,
		slHoundFollow,
		slHoundFaceTarget,
};

IMPLEMENT_CUSTOM_SCHEDULES(CHoundeye, CSquadMonster);

//=========================================================
// GetScheduleOfType 
//=========================================================
Schedule_t* CHoundeye::GetScheduleOfType(int Type)
{
	if (m_fAsleep)
	{
		// if the hound is sleeping, must wake and stand!
		if (HasConditions(bits_COND_HEAR_SOUND))
		{
			CSound* pWakeSound;

			pWakeSound = PBestSound();
			ASSERT(pWakeSound != nullptr);
			if (pWakeSound)
			{
				MakeIdealYaw(pWakeSound->m_vecOrigin);

				if (FLSoundVolume(pWakeSound) >= HOUNDEYE_SOUND_STARTLE_VOLUME)
				{
					// awakened by a loud sound
					return &slHoundWakeUrgent[0];
				}
			}
			// sound was not loud enough to scare the bejesus out of houndeye
			return &slHoundWakeLazy[0];
		}
		else if (HasConditions(bits_COND_NEW_ENEMY))
		{
			// get up fast, to fight.
			return &slHoundWakeUrgent[0];
		}
		else
		{
			// hound is waking up on its own
			return &slHoundWakeLazy[0];
		}
	}

	switch (Type)
	{
	case SCHED_IDLE_STAND:
	{
		// we may want to sleep instead of stand!
		if (InSquad() && !IsLeader() && !m_fAsleep && RANDOM_LONG(0, 29) < 1)
		{
			return &slHoundSleep[0];
		}
		else
		{
			return CSquadMonster::GetScheduleOfType(Type);
		}
	}
	case SCHED_RANGE_ATTACK1:
	{
		return &slHoundRangeAttack[0];
	}
	case SCHED_SPECIAL_ATTACK1:
	{
		return &slHoundSpecialAttack1[0];
	}
	case SCHED_GUARD:
	{
		return &slHoundGuardPack[0];
	}
	case SCHED_HOUND_AGITATED:
	{
		return &slHoundAgitated[0];
	}
	case SCHED_HOUND_HOP_RETREAT:
	{
		return &slHoundHopRetreat[0];
	}
	case SCHED_FAIL:
	{
		if (m_MonsterState == MONSTERSTATE_COMBAT)
		{
			if (!FNullEnt(FIND_CLIENT_IN_PVS(edict())))
			{
				// client in PVS
				return &slHoundCombatFailPVS[0];
			}
			else
			{
				// client has taken off! 
				return &slHoundCombatFailNoPVS[0];
			}
		}
		else
		{
			return CSquadMonster::GetScheduleOfType(Type);
		}
	}
	case SCHED_TARGET_CHASE:
	{
		return &slHoundFollow[0];
	}
	case SCHED_TARGET_FACE:
	{
		return &slHoundFaceTarget[0];
	}
	default:
	{
		return CSquadMonster::GetScheduleOfType(Type);
	}
	}
}

//=========================================================
// GetSchedule 
//=========================================================
Schedule_t* CHoundeye::GetSchedule(void)
{
	switch (m_MonsterState)
	{
	case MONSTERSTATE_COMBAT:
	{
		// dead enemy
		if (HasConditions(bits_COND_ENEMY_DEAD))
		{
			// call base class, all code to handle dead enemies is centralized there.
			return CBaseMonster::GetSchedule();
		}

		if (HasConditions(bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE))
		{
			if (RANDOM_FLOAT(0.0f, 1.0f) <= 0.4f)
			{
				TraceResult tr;
				UTIL_MakeVectors(pev->angles);
				UTIL_TraceHull(pev->origin, pev->origin + gpGlobals->v_forward * -128.0f, dont_ignore_monsters, head_hull, ENT(pev), &tr);

				if (tr.flFraction == 1.0f)
				{
					// it's clear behind, so the hound will jump
					return GetScheduleOfType(SCHED_HOUND_HOP_RETREAT);
				}
			}

			return GetScheduleOfType(SCHED_TAKE_COVER_FROM_ENEMY);
		}

		if (HasConditions(bits_COND_CAN_RANGE_ATTACK1))
		{
			if (OccupySlot(bits_SLOTS_HOUND_ATTACK))
			{
				return GetScheduleOfType(SCHED_RANGE_ATTACK1);
			}

			return GetScheduleOfType(SCHED_HOUND_AGITATED);
		}
		break;
	}

	case MONSTERSTATE_ALERT:
	case MONSTERSTATE_IDLE:
	{
		// If following, return follow schedule
		if (IsFollowing() && m_hTargetEnt != nullptr)
		{
			if (!m_hTargetEnt->IsAlive())
			{
				StopFollowing(FALSE);
				break;
			}
			else
			{
				return GetScheduleOfType(SCHED_TARGET_FACE);
			}
		}
		break;
	}
	}

	return CSquadMonster::GetSchedule();
}

//=========================================================
// FollowerUse - Allow player to command friendly houndeye
//=========================================================
void CHoundeye::FollowerUse(CBaseEntity* pActivator, CBaseEntity* pCaller, USE_TYPE useType, float value)
{
	// Only players can use friendly houndeyes
	if (!pActivator || !pActivator->IsPlayer())
		return;

	// Only friendly houndeyes can be used
	if (!m_bFriendly)
		return;

	// Toggle following behavior
	if (!IsFollowing())
	{
		StartFollowing(pActivator);
		IdleSound();
	}
	else
	{
		StopFollowing(FALSE);
		IdleSound();
	}
}

//=========================================================
// DeclineFollowing - Called when follower can't follow
//=========================================================
void CHoundeye::DeclineFollowing(void)
{
	IdleSound();
}

//=========================================================
// StartFollowing - Begin following a leader
//=========================================================
void CHoundeye::StartFollowing(CBaseEntity* pLeader)
{
	if (pLeader == nullptr)
		return;

	m_hTargetEnt = pLeader;

	if (m_MonsterState == MONSTERSTATE_IDLE)
	{
		SetState(MONSTERSTATE_ALERT);
	}

	m_hEnemy = nullptr;

	ChangeSchedule(GetScheduleOfType(SCHED_TARGET_CHASE));
}

//=========================================================
// IsFollowing - Check if currently following a leader
//=========================================================
BOOL CHoundeye::IsFollowing(void)
{
	if (m_hTargetEnt != nullptr && m_bFriendly)
	{
		return TRUE;
	}
	return FALSE;
}

//=========================================================
// StopFollowing - Stop following the current leader
//=========================================================
void CHoundeye::StopFollowing(BOOL bClearSchedule)
{
	m_hTargetEnt = nullptr;

	if (m_hEnemy != nullptr)
	{
		SetState(MONSTERSTATE_COMBAT);
	}
	else
	{
		SetState(MONSTERSTATE_IDLE);
	}

	if (bClearSchedule)
	{
		ChangeSchedule(GetScheduleOfType(SCHED_IDLE_STAND));
	}
}