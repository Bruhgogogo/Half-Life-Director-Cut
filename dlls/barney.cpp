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
// monster template
//=========================================================
// UNDONE: Holster weapon?

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"monsters.h"
#include	"talkmonster.h"
#include	"schedule.h"
#include	"defaultai.h"
#include	"scripted.h"
#include	"weapons.h"
#include	"soundent.h"

//=========================================================
// Monster's Anim Events Go Here
//=========================================================
// first flag is barney dying for scripted sequences?
enum BarneyAnimEvent
{
	BARNEY_AE_DRAW = 2,
	BARNEY_AE_SHOOT = 3,
	BARNEY_AE_HOLSTER = 4
};

enum BarneyBodyState
{
	BARNEY_BODY_GUNHOLSTERED = 0,
	BARNEY_BODY_GUNDRAWN = 1,
	BARNEY_BODY_GUNGONE = 2
};

// Constants
constexpr float BARNEY_MAX_ATTACK_RANGE = 1024.0f;

constexpr float BARNEY_ATTACK_DOT_THRESHOLD = 0.5f;

constexpr float BARNEY_ATTACK_CHECK_INTERVAL = 0.05f;

constexpr float BARNEY_EYE_HEIGHT = 55.0f;

constexpr float BARNEY_VIEW_OFS_Z = 50.0f;

constexpr float BARNEY_FOLLOW_RANGE = 128.0f;

constexpr float BARNEY_WEAPON_RANGE = 1024.0f;

const float BARNEY_HEV_SURVIVE_CHANCE = 0.4f;

const float BARNEY_MP5_SURVIVE_CHANCE = 0.5f;

const float BARNEY_SHOTGUN_SURVICE_CHANCE = 0.6f;

constexpr float BARNEY_HEV_DEAD_CHANCE = 0.1f;

constexpr float BARNEY_SHOTGUN_COOLDOWN = 0.3f; //I was too tired to adjust animation

// Safe to expanded this if is not enough
const int MAX_HEV_VOICE_WORDS = 64;

struct DamageReductionEntry
{
	int damageType;
	float multiplier;
	char HEVVoice[MAX_HEV_VOICE_WORDS];
};

static DamageReductionEntry g_HEVDamageReduction[] =
{
	{ DMG_BULLET,       0.1f,        "fvox/blood_loss.wav" },
	{ DMG_BLAST,        0.2f,        "fvox/internal_bleeding.wav" },
	{ DMG_BURN,         0.0f,        "fvox/heat_damage.wav" },
	{ DMG_SLOWBURN,     0.0f,        "fvox/heat_damage.wav" },
	{ DMG_ACID,         0.0f,        "fvox/chemical_detected.wav" },
	{ DMG_POISON,       0.0f,        "fvox/biohazard_detected.wav" },
	{ DMG_RADIATION,    0.0f,        "fvox/radiation_detected.wav" },
	{ DMG_CLUB,         0.2f,        "fvox/minor_fracture.wav" },
	{ DMG_CRUSH,        0.3f,        "fvox/major_fracture.wav" },
	{ DMG_FALL,         0.3f,        "fvox/minor_fracture.wav" },
	{ DMG_ENERGYBEAM,   0.5f,        "fvox/major_lacerations.wav" },
	{ DMG_SLASH,        0.4f,        "fvox/minor_lacerations.wav" },
	{ DMG_SONIC,        0.4f,        "fvox/internal_bleeding.wav" },
	{ DMG_SHOCK,        0.4f,        "fvox/shock_damage.wav" },
	{ DMG_PARALYZE,     0.4f,        "fvox/blood_toxins.wav" },
	{ DMG_GENERIC,      0.2f,        "fvox/blood_loss.wav" },
};

class CBarney : public CTalkMonster
{
public:
	void Spawn() override;
	void Precache() override;
	void SetYawSpeed() override;
	int ISoundMask() override;
	void AlertSound() override;
	int Classify() override;
	void HandleAnimEvent(MonsterEvent_t* pEvent) override;

	void RunTask(Task_t* pTask) override;
	void StartTask(Task_t* pTask) override;
	int ObjectCaps() override { return CTalkMonster::ObjectCaps() | FCAP_IMPULSE_USE; }
	int TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType) override;
	BOOL CheckRangeAttack1(float flDot, float flDist) override;

	void DeclineFollowing() override;

	// Override these to set behavior
	Schedule_t* GetScheduleOfType(int Type) override;
	Schedule_t* GetSchedule() override;
	MONSTERSTATE GetIdealState() override;

	void DeathSound() override;
	void PainSound() override;

	void TalkInit();

	void TraceAttack(entvars_t* pevAttacker, float flDamage, Vector vecDir, TraceResult* ptr, int bitsDamageType) override;
	void Killed(entvars_t* pevAttacker, int iGib) override;

	// Save/Restore
	int Save(CSave& save) override;
	int Restore(CRestore& restore) override;
	static TYPEDESCRIPTION m_SaveData[];

	void BarneyFirePistol();

	float GetHEVDamageReduction(int bitsDamageType);
	void HEVSound(int bitsDamageType);

	void KeyValue(KeyValueData* pkvd) override;

	BOOL IsBarneyFriendly() { return m_bEnemyToPlayer; }
private:
	BOOL m_fGunDrawn;
	float m_painTime;
	float m_hevLastReport;
	float m_checkAttackTime;
	float m_shotgunCooldownTime;
	BOOL m_lastAttackCheck;
	float m_flPlayerDamage; // how much pain has the player inflicted on me?

	BOOL m_bEnemyToPlayer; // If TRUE, Barney will attack players and other hostile entities

	enum GuardTypes : int {
		NA = 0,
		HEV = 1,
		MP5 = 2,
		SHOTGUN = 3,
		ORIGINAL = 4
	};

	GuardTypes barneyType = NA;
	GuardTypes overrideType = NA;

	CUSTOM_SCHEDULES;
};

LINK_ENTITY_TO_CLASS(monster_barney, CBarney);

TYPEDESCRIPTION CBarney::m_SaveData[] =
{
	DEFINE_FIELD(CBarney, m_fGunDrawn, FIELD_BOOLEAN),
	DEFINE_FIELD(CBarney, m_painTime, FIELD_TIME),
	DEFINE_FIELD(CBarney, m_hevLastReport, FIELD_TIME),
	DEFINE_FIELD(CBarney, m_checkAttackTime, FIELD_TIME),
	DEFINE_FIELD(CBarney, m_shotgunCooldownTime, FIELD_TIME),
	DEFINE_FIELD(CBarney, m_lastAttackCheck, FIELD_BOOLEAN),
	DEFINE_FIELD(CBarney, m_flPlayerDamage, FIELD_FLOAT),
	DEFINE_FIELD(CBarney, barneyType, FIELD_INTEGER),
	DEFINE_FIELD(CBarney, m_bEnemyToPlayer, FIELD_BOOLEAN),
};

IMPLEMENT_SAVERESTORE(CBarney, CTalkMonster);

//=========================================================
// AI Schedules Specific to this monster
//=========================================================
Task_t tlBaFollow[] =
{
	{ TASK_MOVE_TO_TARGET_RANGE, BARNEY_FOLLOW_RANGE }, // Move within 128 of target ent (client)
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_TARGET_FACE) },
};

Schedule_t slBaFollow[] =
{
	{
		tlBaFollow,
		ARRAYSIZE(tlBaFollow),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_PROVOKED,
		bits_SOUND_DANGER,
		"Follow"
	},
};

//=========================================================
// BarneyDraw- much better looking draw schedule for when
// barney knows who he's gonna attack.
//=========================================================
Task_t tlBarneyEnemyDraw[] =
{
	{ TASK_STOP_MOVING, 0 },
	{ TASK_FACE_ENEMY, 0 },
	{ TASK_PLAY_SEQUENCE_FACE_ENEMY, static_cast<float>(ACT_ARM) },
};

Schedule_t slBarneyEnemyDraw[] =
{
	{
		tlBarneyEnemyDraw,
		ARRAYSIZE(tlBarneyEnemyDraw),
		0,
		0,
		"Barney Enemy Draw"
	}
};

Task_t tlBaFaceTarget[] =
{
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_FACE_TARGET, 0.0f },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_SET_SCHEDULE, static_cast<float>(SCHED_TARGET_CHASE) },
};

Schedule_t slBaFaceTarget[] =
{
	{
		tlBaFaceTarget,
		ARRAYSIZE(tlBaFaceTarget),
		bits_COND_CLIENT_PUSH |
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_PROVOKED,
		bits_SOUND_DANGER,
		"FaceTarget"
	},
};

Task_t tlIdleBaStand[] =
{
	{ TASK_STOP_MOVING, 0 },
	{ TASK_SET_ACTIVITY, static_cast<float>(ACT_IDLE) },
	{ TASK_WAIT, 2.0f }, // repick IDLESTAND every two seconds.
	{ TASK_TLK_HEADRESET, 0.0f }, // reset head position
};

Schedule_t slIdleBaStand[] =
{
	{
		tlIdleBaStand,
		ARRAYSIZE(tlIdleBaStand),
		bits_COND_NEW_ENEMY |
		bits_COND_LIGHT_DAMAGE |
		bits_COND_HEAVY_DAMAGE |
		bits_COND_HEAR_SOUND |
		bits_COND_SMELL |
		bits_COND_PROVOKED,
		bits_SOUND_COMBAT | // sound flags - change these, and you'll break the talking code.
	//bits_SOUND_PLAYER |
	//bits_SOUND_WORLD |
	bits_SOUND_DANGER |
	bits_SOUND_MEAT | // scents
	bits_SOUND_CARCASS |
	bits_SOUND_GARBAGE,
	"IdleStand"
},
};

DEFINE_CUSTOM_SCHEDULES(CBarney)
{
	slBaFollow,
		slBarneyEnemyDraw,
		slBaFaceTarget,
		slIdleBaStand,
};

IMPLEMENT_CUSTOM_SCHEDULES(CBarney, CTalkMonster);

void CBarney::KeyValue(KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "barney_type_override"))
	{
		overrideType = static_cast<GuardTypes>(atoi(pkvd->szValue));
		pkvd->fHandled = TRUE;
	}
	else if (FStrEq(pkvd->szKeyName, "is_enemy_to_player"))
	{
		m_bEnemyToPlayer = static_cast<BOOL>(atoi(pkvd->szValue));
		pkvd->fHandled = TRUE;
	}
	else
	{
		CTalkMonster::KeyValue(pkvd);
	}
}

void CBarney::StartTask(Task_t* pTask)
{
	CTalkMonster::StartTask(pTask);
}

void CBarney::RunTask(Task_t* pTask)
{
	switch (pTask->iTask)
	{
	case TASK_RANGE_ATTACK1:
		if (m_hEnemy != NULL && (m_hEnemy->IsPlayer()))
		{
			pev->framerate = 1.5f;
		}
		CTalkMonster::RunTask(pTask);
		break;
	default:
		CTalkMonster::RunTask(pTask);
		break;
	}
}

//=========================================================
// ISoundMask - returns a bit mask indicating which types
// of sounds this monster regards. 
//=========================================================
int CBarney::ISoundMask()
{
	return bits_SOUND_WORLD |
		bits_SOUND_COMBAT |
		bits_SOUND_CARCASS |
		bits_SOUND_MEAT |
		bits_SOUND_GARBAGE |
		bits_SOUND_DANGER |
		bits_SOUND_PLAYER;
}

//=========================================================
// Classify - indicates this monster's place in the 
// relationship table.
//=========================================================
int CBarney::Classify()
{
	// If enemy to player, treat as hostile monster
	if (m_bEnemyToPlayer)
		return CLASS_ALIEN_MONSTER;

	return CLASS_PLAYER_ALLY;
}

//=========================================================
// ALertSound - barney says "Freeze!"
//=========================================================
void CBarney::AlertSound()
{
	if (m_hEnemy != NULL)
	{
		if (FOkToSpeak())
		{
			PlaySentence("BA_ATTACK", RANDOM_FLOAT(2.8f, 3.2f), VOL_NORM, ATTN_IDLE);
		}
	}
}

//=========================================================
// SetYawSpeed - allows each sequence to have a different
// turn rate associated with it.
//=========================================================
void CBarney::SetYawSpeed()
{
	int ys = 0;

	switch (m_Activity)
	{
	case ACT_IDLE:
		ys = 90;
		break;
	case ACT_WALK:
		ys = 90;
		break;
	case ACT_RUN:
		ys = 120;
		break;
	default:
		ys = 90;
		break;
	}

	pev->yaw_speed = ys;
}

//=========================================================
// CheckRangeAttack1
//=========================================================
BOOL CBarney::CheckRangeAttack1(float flDot, float flDist)
{
	if (flDist <= BARNEY_MAX_ATTACK_RANGE && flDot >= BARNEY_ATTACK_DOT_THRESHOLD && gpGlobals->time > m_shotgunCooldownTime)
	{
		if (gpGlobals->time > m_checkAttackTime)
		{
			TraceResult tr;

			Vector shootOrigin = pev->origin + Vector(0.0f, 0.0f, BARNEY_EYE_HEIGHT);
			CBaseEntity* pEnemy = m_hEnemy;
			Vector shootTarget = ((pEnemy->BodyTarget(shootOrigin) - pEnemy->pev->origin) + m_vecEnemyLKP);
			UTIL_TraceLine(shootOrigin, shootTarget, dont_ignore_monsters, ENT(pev), &tr);
			m_lastAttackCheck = (tr.flFraction == 1.0f || (tr.pHit != NULL && CBaseEntity::Instance(tr.pHit) == pEnemy));
			m_checkAttackTime = gpGlobals->time + BARNEY_ATTACK_CHECK_INTERVAL;
		}
		return m_lastAttackCheck;
	}
	if (barneyType == SHOTGUN) m_shotgunCooldownTime = gpGlobals->time + BARNEY_SHOTGUN_COOLDOWN;
	return FALSE;
}

//=========================================================
// BarneyFirePistol - shoots one round from the pistol at
// the enemy barney is facing.
//=========================================================
void CBarney::BarneyFirePistol()
{
	UTIL_MakeVectors(pev->angles);
	Vector vecShootOrigin = pev->origin + Vector(0.0f, 0.0f, BARNEY_EYE_HEIGHT);
	Vector vecShootDir = ShootAtEnemy(vecShootOrigin);

	Vector angDir = UTIL_VecToAngles(vecShootDir);
	SetBlending(0, angDir.x);
	pev->effects = EF_MUZZLEFLASH;

	switch (barneyType) {
	case HEV:
	{
		FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_2DEGREES, BARNEY_WEAPON_RANGE, BULLET_MONSTER_357);
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "barney/ba_attack3.wav", 1.0f, ATTN_NORM);
		break;
	}
	case MP5:
		FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_2DEGREES, BARNEY_WEAPON_RANGE, BULLET_MONSTER_MP5);
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "barney/ba_attack5.wav", 1.0f, ATTN_NORM);
		break;
	case SHOTGUN:
		FireBullets(6, vecShootOrigin, vecShootDir, VECTOR_CONE_15DEGREES, BARNEY_WEAPON_RANGE, BULLET_MONSTER_BUCKSHOT, 0);
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "barney/ba_attack4.wav", 1.0f, ATTN_NORM);
		break;
	case ORIGINAL:
	{
		FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_2DEGREES, BARNEY_WEAPON_RANGE, BULLET_MONSTER_9MM);
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "barney/ba_attack2.wav", 1.0f, ATTN_NORM);
		break;
	}
	default:
	{
		FireBullets(1, vecShootOrigin, vecShootDir, VECTOR_CONE_2DEGREES, BARNEY_WEAPON_RANGE, BULLET_MONSTER_9MM);
		EMIT_SOUND(ENT(pev), CHAN_WEAPON, "barney/ba_attack2.wav", 1.0f, ATTN_NORM);
		break;
	}
	}

	CSoundEnt::InsertSound(bits_SOUND_COMBAT, pev->origin, 384, 0.3f);
}

//=========================================================
// HandleAnimEvent - catches the monster-specific messages
// that occur when tagged animation frames are played.
//
// Returns number of events handled, 0 if none.
//=========================================================
void CBarney::HandleAnimEvent(MonsterEvent_t* pEvent)
{
	switch (pEvent->event)
	{
	case BARNEY_AE_SHOOT:
		BarneyFirePistol();
		break;

	case BARNEY_AE_DRAW:
		// barney's bodygroup switches here so he can pull gun from holster

		// Hey im sorry for this because my MP5 and Shotgun barney model are too shit!
		if (barneyType != MP5 && barneyType != SHOTGUN) pev->body = BARNEY_BODY_GUNDRAWN;
		m_fGunDrawn = TRUE;
		break;

	case BARNEY_AE_HOLSTER:
		// change bodygroup to replace gun in holster
		if (barneyType != MP5 && barneyType != SHOTGUN) pev->body = BARNEY_BODY_GUNHOLSTERED;
		m_fGunDrawn = FALSE;
		break;

	default:
		CTalkMonster::HandleAnimEvent(pEvent);
		break;
	}
}

//=========================================================
// Spawn
//=========================================================
void CBarney::Spawn()
{
	Precache();

	if (overrideType == NA)
	{
		float randomResult = RANDOM_FLOAT(0.0f, 1.0f);
		float totalChance = BARNEY_HEV_SURVIVE_CHANCE + BARNEY_MP5_SURVIVE_CHANCE + BARNEY_SHOTGUN_SURVICE_CHANCE;
		float hevChance = BARNEY_HEV_SURVIVE_CHANCE / totalChance;
		float mp5Chance = BARNEY_MP5_SURVIVE_CHANCE / totalChance;
		float shotgunChance = BARNEY_SHOTGUN_SURVICE_CHANCE / totalChance;

		if (randomResult < hevChance) {
			SET_MODEL(ENT(pev), "models/barney_hev.mdl");
			barneyType = HEV;
		}
		else if (randomResult < hevChance + mp5Chance) {
			SET_MODEL(ENT(pev), "models/barney_mp5.mdl");
			barneyType = MP5;
		}
		else if (randomResult < hevChance + mp5Chance + shotgunChance) {
			SET_MODEL(ENT(pev), "models/barney_shotgun.mdl");
			barneyType = SHOTGUN;
		}
		else {
			SET_MODEL(ENT(pev), "models/barney.mdl");
			barneyType = ORIGINAL;
		}
	}
	else
	{
		switch (overrideType)
		{
		case ORIGINAL:
			SET_MODEL(ENT(pev), "models/barney.mdl");
			barneyType = ORIGINAL;
			break;
		case HEV:
			SET_MODEL(ENT(pev), "models/barney_hev.mdl");
			barneyType = HEV;
			break;
		case MP5:
			SET_MODEL(ENT(pev), "models/barney_mp5.mdl");
			barneyType = MP5;
			break;
		case SHOTGUN:
			SET_MODEL(ENT(pev), "models/barney_shotgun.mdl");
			barneyType = SHOTGUN;
			break;
		default:
			SET_MODEL(ENT(pev), "models/barney.mdl");
			barneyType = ORIGINAL;
			break;
		}
	}
	UTIL_SetSize(pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);

	pev->solid = SOLID_SLIDEBOX;
	pev->movetype = MOVETYPE_STEP;
	m_bloodColor = BLOOD_COLOR_RED;
	pev->health = gSkillData.barneyHealth;
	pev->view_ofs = Vector(0.0f, 0.0f, BARNEY_VIEW_OFS_Z); // position of the eyes relative to monster's origin.
	m_flFieldOfView = VIEW_FIELD_WIDE; // NOTE: we need a wide field of view so npc will notice player and say hello
	m_MonsterState = MONSTERSTATE_NONE;

	pev->body = BARNEY_BODY_GUNHOLSTERED; // gun in holster
	m_fGunDrawn = FALSE;

	m_afCapability = bits_CAP_HEAR | bits_CAP_TURN_HEAD | bits_CAP_DOORS_GROUP;

	MonsterInit();

	// Only allow following if Barney is a player ally
	if (!m_bEnemyToPlayer)
	{
		SetUse(&CBarney::FollowerUse);
	}
	else
	{
		SetUse(NULL);
	}
}

//=========================================================
// Precache - precaches all resources this monster needs
//=========================================================
void CBarney::Precache()
{
	PRECACHE_MODEL("models/barney.mdl");
	PRECACHE_MODEL("models/barney_hev.mdl");
	PRECACHE_MODEL("models/barney_mp5.mdl");
	PRECACHE_MODEL("models/barney_shotgun.mdl");

	PRECACHE_SOUND("barney/ba_attack1.wav");
	PRECACHE_SOUND("barney/ba_attack2.wav"); // 9mm glock
	PRECACHE_SOUND("barney/ba_attack3.wav"); // Python
	PRECACHE_SOUND("barney/ba_attack4.wav"); // Shotgun
	PRECACHE_SOUND("barney/ba_attack5.wav"); // MP5

	PRECACHE_SOUND("barney/ba_pain1.wav");
	PRECACHE_SOUND("barney/ba_pain2.wav");
	PRECACHE_SOUND("barney/ba_pain3.wav");

	PRECACHE_SOUND("barney/ba_die1.wav");
	PRECACHE_SOUND("barney/ba_die2.wav");
	PRECACHE_SOUND("barney/ba_die3.wav");

	for (int i = 0; i < ARRAYSIZE(g_HEVDamageReduction); i++)
	{
		char* voice = g_HEVDamageReduction[i].HEVVoice;
		PRECACHE_SOUND(voice);
	}

	// every new barney must call this, otherwise
	// when a level is loaded, nobody will talk (time is reset to 0)
	TalkInit();
	CTalkMonster::Precache();
}

// Init talk data
void CBarney::TalkInit()
{
	CTalkMonster::TalkInit();

	// scientists speach group names (group names are in sentences.txt)

	m_szGrp[TLK_ANSWER] = "BA_ANSWER";
	m_szGrp[TLK_QUESTION] = "BA_QUESTION";
	m_szGrp[TLK_IDLE] = "BA_IDLE";
	m_szGrp[TLK_STARE] = "BA_STARE";
	m_szGrp[TLK_USE] = "BA_OK";
	m_szGrp[TLK_UNUSE] = "BA_WAIT";
	m_szGrp[TLK_STOP] = "BA_STOP";

	m_szGrp[TLK_NOSHOOT] = "BA_SCARED";
	m_szGrp[TLK_HELLO] = "BA_HELLO";

	m_szGrp[TLK_PLHURT1] = "!BA_CUREA";
	m_szGrp[TLK_PLHURT2] = "!BA_CUREB";
	m_szGrp[TLK_PLHURT3] = "!BA_CUREC";

	m_szGrp[TLK_PHELLO] = NULL; // "BA_PHELLO"; // UNDONE
	m_szGrp[TLK_PIDLE] = NULL; // "BA_PIDLE"; // UNDONE
	m_szGrp[TLK_PQUESTION] = "BA_PQUEST"; // UNDONE

	m_szGrp[TLK_SMELL] = "BA_SMELL";

	m_szGrp[TLK_WOUND] = "BA_WOUND";
	m_szGrp[TLK_MORTAL] = "BA_MORTAL";

	// get voice for head - just one barney voice for now
	m_voicePitch = 100;
}

static BOOL IsFacing(entvars_t* pevTest, const Vector& reference)
{
	Vector vecDir = (reference - pevTest->origin);
	vecDir.z = 0.0f;
	vecDir = vecDir.Normalize();
	Vector forward, angle;
	angle = pevTest->v_angle;
	angle.x = 0.0f;
	UTIL_MakeVectorsPrivate(angle, forward, NULL, NULL);
	// He's facing me, he meant it
	if (DotProduct(forward, vecDir) > 0.96f) // +/- 15 degrees or so
	{
		return TRUE;
	}
	return FALSE;
}

float CBarney::GetHEVDamageReduction(int bitsDamageType)
{
	for (int i = 0; i < ARRAYSIZE(g_HEVDamageReduction); i++)
	{
		if (bitsDamageType & g_HEVDamageReduction[i].damageType)
		{
			return g_HEVDamageReduction[i].multiplier;
		}
	}
	return 1.0f; // Don't reduce if not find
}

void CBarney::HEVSound(int bitsDamageType)
{
	if (gpGlobals->time < m_hevLastReport)
		return;

	m_hevLastReport = gpGlobals->time + RANDOM_FLOAT(5.0f, 7.0f);

	for (int i = 0; i < ARRAYSIZE(g_HEVDamageReduction); i++)
	{
		if (bitsDamageType & g_HEVDamageReduction[i].damageType)
		{
			EMIT_SOUND(ENT(pev), CHAN_BODY, g_HEVDamageReduction[i].HEVVoice, 1.0f, ATTN_NORM);
		}
	}
}


int CBarney::TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker, float flDamage, int bitsDamageType)
{
	float finalDamage = flDamage;
	if (barneyType == HEV && IsAlive() == TRUE)
	{
		finalDamage *= 0.5f;
		finalDamage *= GetHEVDamageReduction(bitsDamageType);
		HEVSound(bitsDamageType);
	}

	// make sure friends talk about it if player hurts talkmonsters...
	int ret = CTalkMonster::TakeDamage(pevInflictor, pevAttacker, finalDamage, bitsDamageType);
	if (!IsAlive() || pev->deadflag == DEAD_DYING)
		return ret;

	if (m_MonsterState != MONSTERSTATE_PRONE && (pevAttacker->flags & FL_CLIENT))
	{
		m_flPlayerDamage += finalDamage;

		// This is a heuristic to determine if the player intended to harm me
		// If I have an enemy, we can't establish intent (may just be crossfire)
		if (m_hEnemy == NULL)
		{
			// If the player was facing directly at me, or I'm already suspicious, get mad
			if ((m_afMemory & bits_MEMORY_SUSPICIOUS) || IsFacing(pevAttacker, pev->origin))
			{
				// If Barney is a player ally, getting shot by player makes him hostile
				if (!m_bEnemyToPlayer)
				{
					// Alright, now I'm pissed! Turn on the player
					m_bEnemyToPlayer = TRUE;
					PlaySentence("BA_MAD", 4.0f, VOL_NORM, ATTN_NORM);

					Remember(bits_MEMORY_PROVOKED);
					StopFollowing(TRUE);

					// Force enemy to be the player who shot us
					CBaseEntity* pAttacker = CBaseEntity::Instance(pevAttacker);
					if (pAttacker)
					{
						m_hEnemy = pAttacker;
					}
				}
				else
				{
					// Already hostile, just play the mad sound
					PlaySentence("BA_MAD", 4.0f, VOL_NORM, ATTN_NORM);
				}
			}
			else
			{
				// Hey, be careful with that
				PlaySentence("BA_SHOT", 4.0f, VOL_NORM, ATTN_NORM);
				Remember(bits_MEMORY_SUSPICIOUS);
			}
		}
		else if (!(m_hEnemy->IsPlayer()) && pev->deadflag == DEAD_NO)
		{
			PlaySentence("BA_SHOT", 4.0f, VOL_NORM, ATTN_NORM);
		}
	}

	return ret;
}

//=========================================================
// PainSound
//=========================================================
void CBarney::PainSound()
{
	if (gpGlobals->time < m_painTime)
		return;

	m_painTime = gpGlobals->time + RANDOM_FLOAT(0.5f, 0.75f);

	switch (RANDOM_LONG(0, 2))
	{
	case 0: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_pain1.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	case 1: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_pain2.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	case 2: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_pain3.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	}
}

//=========================================================
// DeathSound 
//=========================================================
void CBarney::DeathSound()
{
	switch (RANDOM_LONG(0, 2))
	{
	case 0: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_die1.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	case 1: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_die2.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	case 2: EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "barney/ba_die3.wav", 1.0f, ATTN_NORM, 0, GetVoicePitch()); break;
	}
}

void CBarney::TraceAttack(entvars_t* pevAttacker, float flDamage, Vector vecDir, TraceResult* ptr, int bitsDamageType)
{
	switch (ptr->iHitgroup)
	{
	case HITGROUP_CHEST:
	case HITGROUP_STOMACH:
		if (bitsDamageType & (DMG_BULLET | DMG_SLASH | DMG_BLAST))
		{
			flDamage = flDamage / 2.0f;
		}
		break;
	case 10:
		if (bitsDamageType & (DMG_BULLET | DMG_SLASH | DMG_CLUB))
		{
			flDamage -= 20.0f;
			if (flDamage <= 0.0f)
			{
				UTIL_Ricochet(ptr->vecEndPos, 1.0f);
				flDamage = 0.01f;
			}
		}
		// always a head shot
		ptr->iHitgroup = HITGROUP_HEAD;
		break;
	}

	CTalkMonster::TraceAttack(pevAttacker, flDamage, vecDir, ptr, bitsDamageType);
}

// Sorry for that because my mod problem mp5 wont gone!
void CBarney::Killed(entvars_t* pevAttacker, int iGib)
{
	if (pev->body < BARNEY_BODY_GUNGONE)
	{ // drop the gun!
		pev->body = BARNEY_BODY_GUNGONE;

		Vector vecGunPos;
		Vector vecGunAngles;

		GetAttachment(0, vecGunPos, vecGunAngles);

		switch (barneyType) {
		case ORIGINAL:
			DropItem("weapon_9mmhandgun", vecGunPos, vecGunAngles);
			break;
		case HEV:
			DropItem("weapon_357", vecGunPos, vecGunAngles);
			break;
		case MP5:
			DropItem("weapon_mp5", vecGunPos, vecGunAngles);
			break;
		case SHOTGUN:
			DropItem("weapon_shotgun", vecGunPos, vecGunAngles);
			break;
		}

	}

	SetUse(NULL);
	CTalkMonster::Killed(pevAttacker, iGib);
}

//=========================================================
// AI Schedules Specific to this monster
//=========================================================

Schedule_t* CBarney::GetScheduleOfType(int Type)
{
	Schedule_t* psched;

	switch (Type)
	{
	case SCHED_ARM_WEAPON:
		if (m_hEnemy != NULL)
		{
			// face enemy, then draw.
			return slBarneyEnemyDraw;
		}
		break;

		// Hook these to make a looping schedule
	case SCHED_TARGET_FACE:
		// call base class default so that barney will talk
		// when 'used' 
		psched = CTalkMonster::GetScheduleOfType(Type);

		if (psched == slIdleStand)
			return slBaFaceTarget; // override this for different target face behavior
		else
			return psched;

	case SCHED_TARGET_CHASE:
		return slBaFollow;

	case SCHED_IDLE_STAND:
		// call base class default so that scientist will talk
		// when standing during idle
		psched = CTalkMonster::GetScheduleOfType(Type);

		if (psched == slIdleStand)
		{
			// just look straight ahead.
			return slIdleBaStand;
		}
		else
			return psched;
	}

	return CTalkMonster::GetScheduleOfType(Type);
}

//=========================================================
// GetSchedule - Decides which type of schedule best suits
// the monster's current state and conditions. Then calls
// monster's member function to get a pointer to a schedule
// of the proper type.
//=========================================================
Schedule_t* CBarney::GetSchedule()
{
	if (HasConditions(bits_COND_HEAR_SOUND))
	{
		CSound* pSound = PBestSound();

		ASSERT(pSound != NULL);
		if (pSound && (pSound->m_iType & bits_SOUND_DANGER))
			return GetScheduleOfType(SCHED_TAKE_COVER_FROM_BEST_SOUND);
	}
	if (HasConditions(bits_COND_ENEMY_DEAD) && FOkToSpeak())
	{
		PlaySentence("BA_KILL", 4.0f, VOL_NORM, ATTN_NORM);
	}

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

		// always act surprized with a new enemy
		if (HasConditions(bits_COND_NEW_ENEMY) && HasConditions(bits_COND_LIGHT_DAMAGE))
			return GetScheduleOfType(SCHED_SMALL_FLINCH);

		// wait for one schedule to draw gun
		if (!m_fGunDrawn)
			return GetScheduleOfType(SCHED_ARM_WEAPON);

		if (HasConditions(bits_COND_HEAVY_DAMAGE))
			return GetScheduleOfType(SCHED_TAKE_COVER_FROM_ENEMY);
	}
	break;

	case MONSTERSTATE_ALERT:
	case MONSTERSTATE_IDLE:
		if (HasConditions(bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE))
		{
			// flinch if hurt
			return GetScheduleOfType(SCHED_SMALL_FLINCH);
		}

		// Only allow following behavior if Barney is a player ally
		if (m_hEnemy == NULL && IsFollowing() && !m_bEnemyToPlayer)
		{
			if (!m_hTargetEnt->IsAlive())
			{
				// UNDONE: Comment about the recently dead player here?
				StopFollowing(FALSE);
				break;
			}
			else
			{
				if (HasConditions(bits_COND_CLIENT_PUSH))
				{
					return GetScheduleOfType(SCHED_MOVE_AWAY_FOLLOW);
				}
				return GetScheduleOfType(SCHED_TARGET_FACE);
			}
		}

		if (HasConditions(bits_COND_CLIENT_PUSH))
		{
			return GetScheduleOfType(SCHED_MOVE_AWAY);
		}

		// try to say something about smells
		TrySmellTalk();
		break;
	}

	return CTalkMonster::GetSchedule();
}

MONSTERSTATE CBarney::GetIdealState()
{
	return CTalkMonster::GetIdealState();
}

void CBarney::DeclineFollowing()
{
	PlaySentence("BA_POK", 2.0f, VOL_NORM, ATTN_NORM);
}

//=========================================================
// DEAD BARNEY PROP
//
// Designer selects a pose in worldcraft, 0 through num_poses-1
// this value is added to what is selected as the 'first dead pose'
// among the monster's normal animations. All dead poses must
// appear sequentially in the model file. Be sure and set
// the m_iFirstPose properly!
//
//=========================================================
class CDeadBarney : public CBaseMonster
{
public:
	void Spawn() override;
	int Classify() override { return CLASS_PLAYER_ALLY; }

	void KeyValue(KeyValueData* pkvd) override;

private:
	int m_iPose; // which sequence to display -- temporary, don't need to save
	static const char* m_szPoses[3];
};

const char* CDeadBarney::m_szPoses[3] = { "lying_on_back", "lying_on_side", "lying_on_stomach" };

LINK_ENTITY_TO_CLASS(monster_barney_dead, CDeadBarney);

void CDeadBarney::KeyValue(KeyValueData* pkvd)
{
	if (FStrEq(pkvd->szKeyName, "pose"))
	{
		m_iPose = atoi(pkvd->szValue);
		pkvd->fHandled = TRUE;
	}
	else
	{
		CBaseMonster::KeyValue(pkvd);
	}
}

//=========================================================
// ********** DeadBarney SPAWN **********
//=========================================================
void CDeadBarney::Spawn()
{
	if (RANDOM_FLOAT(0.0f, 1.0f) < BARNEY_HEV_DEAD_CHANCE)
	{
		PRECACHE_MODEL("models/barney_hev.mdl");
		SET_MODEL(ENT(pev), "models/barney_hev.mdl");
	}
	else
	{
		PRECACHE_MODEL("models/barney.mdl");
		SET_MODEL(ENT(pev), "models/barney.mdl");
	}

	pev->effects = 0;
	pev->yaw_speed = 8;
	pev->sequence = 0;
	m_bloodColor = BLOOD_COLOR_RED;

	pev->sequence = LookupSequence(m_szPoses[m_iPose]);
	if (pev->sequence == -1)
	{
		ALERT(at_console, "Dead barney with bad pose\n");
	}
	// Corpses have less health
	pev->health = 8.0f; // gSkillData.barneyHealth;

	MonsterInitDead();
}