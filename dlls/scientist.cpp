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
// human scientist (passive lab worker)
//=========================================================

#include	"extdll.h"
#include	"util.h"
#include	"cbase.h"
#include	"monsters.h"
#include	"talkmonster.h"
#include	"schedule.h"
#include	"defaultai.h"
#include	"scripted.h"
#include	"animation.h"
#include	"soundent.h"


//=========================================================
// Constants
//=========================================================

// Number of heads available for scientist model
#define		NUM_SCIENTIST_HEADS		4

// Scientist head types
enum
{
    HEAD_GLASSES = 0,
    HEAD_EINSTEIN = 1,
    HEAD_LUTHER = 2,
    HEAD_SLICK = 3
};

// Custom schedule types
enum
{
    SCHED_HIDE = LAST_TALKMONSTER_SCHEDULE + 1,
    SCHED_FEAR,
    SCHED_PANIC,
    SCHED_STARTLE,
    SCHED_TARGET_CHASE_SCARED,
    SCHED_TARGET_FACE_SCARED,
};

// Custom task types
enum
{
    TASK_SAY_HEAL = LAST_TALKMONSTER_TASK + 1,
    TASK_HEAL,
    TASK_SAY_FEAR,
    TASK_RUN_PATH_SCARED,
    TASK_SCREAM,
    TASK_RANDOM_SCREAM,
    TASK_MOVE_TO_TARGET_RANGE_SCARED,
};

// Monster's Anim Events
#define		SCIENTIST_AE_HEAL		( 1 )
#define		SCIENTIST_AE_NEEDLEON	( 2 )
#define		SCIENTIST_AE_NEEDLEOFF	( 3 )

// Fear timeout duration
constexpr float FEAR_TIMEOUT = 15.0f;

//=========================================================
// CScientist - Passive lab worker
//=========================================================

class CScientist : public CTalkMonster
{
public:
    // Lifecycle
    void Spawn(void);
    void Precache(void);

    // Movement and behavior
    void SetYawSpeed(void);
    int Classify(void);
    void HandleAnimEvent(MonsterEvent_t* pEvent);
    void RunTask(Task_t* pTask);
    void StartTask(Task_t* pTask);

    int ObjectCaps(void)
    {
        return CTalkMonster::ObjectCaps() | FCAP_IMPULSE_USE;
    }

    int TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker,
        float flDamage, int bitsDamageType);

    int FriendNumber(int arrayNumber);
    void SetActivity(Activity newActivity);
    Activity GetStoppedActivity(void);
    int ISoundMask(void);
    void DeclineFollowing(void);

    float CoverRadius(void) { return 1200.0f; }

    BOOL DisregardEnemy(CBaseEntity* pEnemy)
    {
        return !pEnemy->IsAlive() || (gpGlobals->time - m_fearTime) > FEAR_TIMEOUT;
    }

    // Healing and reactions
    BOOL CanHeal(void);
    void Heal(void);
    void Scream(void);

    // Schedule management
    Schedule_t* GetScheduleOfType(int Type);
    Schedule_t* GetSchedule(void);
    MONSTERSTATE GetIdealState(void);

    // Sounds
    void DeathSound(void);
    void PainSound(void);
    void TalkInit(void);

    // Model
    char* GetScientistModel(void) const;

    void Killed(entvars_t* pevAttacker, int iGib);

    int Save(CSave& save);
    int Restore(CRestore& restore);

    static TYPEDESCRIPTION m_SaveData[];

    CUSTOM_SCHEDULES;

private:
    float m_painTime;
    float m_healTime;
    float m_fearTime;
};

LINK_ENTITY_TO_CLASS(monster_scientist, CScientist);

TYPEDESCRIPTION CScientist::m_SaveData[] =
{
    DEFINE_FIELD(CScientist, m_painTime, FIELD_TIME),
    DEFINE_FIELD(CScientist, m_healTime, FIELD_TIME),
    DEFINE_FIELD(CScientist, m_fearTime, FIELD_TIME),
};

IMPLEMENT_SAVERESTORE(CScientist, CTalkMonster);

//=========================================================
// Task Definitions
//=========================================================

// Follow task list
Task_t tlFollow[] =
{
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_CANT_FOLLOW },
    { TASK_MOVE_TO_TARGET_RANGE, 128.0f },
};

Schedule_t slFollow[] =
{
    {
        tlFollow,
        ARRAYSIZE(tlFollow),
        bits_COND_NEW_ENEMY | bits_COND_LIGHT_DAMAGE |
        bits_COND_HEAVY_DAMAGE | bits_COND_HEAR_SOUND,
        bits_SOUND_COMBAT | bits_SOUND_DANGER,
        "Follow"
    },
};

// Follow scared task list
Task_t tlFollowScared[] =
{
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_TARGET_CHASE },
    { TASK_MOVE_TO_TARGET_RANGE_SCARED, 128.0f },
};

Schedule_t slFollowScared[] =
{
    {
        tlFollowScared,
        ARRAYSIZE(tlFollowScared),
        bits_COND_NEW_ENEMY | bits_COND_HEAR_SOUND |
        bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE,
        bits_SOUND_DANGER,
        "FollowScared"
    },
};

// Face target scared task list
Task_t tlFaceTargetScared[] =
{
    { TASK_FACE_TARGET, 0.0f },
    { TASK_SET_ACTIVITY, (float)ACT_CROUCHIDLE },
    { TASK_SET_SCHEDULE, (float)SCHED_TARGET_CHASE_SCARED },
};

Schedule_t slFaceTargetScared[] =
{
    {
        tlFaceTargetScared,
        ARRAYSIZE(tlFaceTargetScared),
        bits_COND_HEAR_SOUND | bits_COND_NEW_ENEMY,
        bits_SOUND_DANGER,
        "FaceTargetScared"
    },
};

// Stop following task list
Task_t tlStopFollowing[] =
{
    { TASK_CANT_FOLLOW, 0.0f },
};

Schedule_t slStopFollowing[] =
{
    {
        tlStopFollowing,
        ARRAYSIZE(tlStopFollowing),
        0,
        0,
        "StopFollowing"
    },
};

// Heal task list
Task_t tlHeal[] =
{
    { TASK_MOVE_TO_TARGET_RANGE, 50.0f },
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_TARGET_CHASE },
    { TASK_FACE_IDEAL, 0.0f },
    { TASK_SAY_HEAL, 0.0f },
    { TASK_PLAY_SEQUENCE_FACE_TARGET, (float)ACT_ARM },
    { TASK_HEAL, 0.0f },
    { TASK_PLAY_SEQUENCE_FACE_TARGET, (float)ACT_DISARM },
};

Schedule_t slHeal[] =
{
    {
        tlHeal,
        ARRAYSIZE(tlHeal),
        0,
        0,
        "Heal"
    },
};

// Face target task list
Task_t tlFaceTarget[] =
{
    { TASK_STOP_MOVING, 0.0f },
    { TASK_FACE_TARGET, 0.0f },
    { TASK_SET_ACTIVITY, (float)ACT_IDLE },
    { TASK_SET_SCHEDULE, (float)SCHED_TARGET_CHASE },
};

Schedule_t slFaceTarget[] =
{
    {
        tlFaceTarget,
        ARRAYSIZE(tlFaceTarget),
        bits_COND_CLIENT_PUSH | bits_COND_NEW_ENEMY | bits_COND_HEAR_SOUND,
        bits_SOUND_COMBAT | bits_SOUND_DANGER,
        "FaceTarget"
    },
};

// Panic task list
Task_t tlSciPanic[] =
{
    { TASK_STOP_MOVING, 0.0f },
    { TASK_FACE_ENEMY, 0.0f },
    { TASK_SCREAM, 0.0f },
    { TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_EXCITED },
    { TASK_SET_ACTIVITY, (float)ACT_IDLE },
};

Schedule_t slSciPanic[] =
{
    {
        tlSciPanic,
        ARRAYSIZE(tlSciPanic),
        0,
        0,
        "SciPanic"
    },
};

// Idle stand task list
Task_t tlIdleSciStand[] =
{
    { TASK_STOP_MOVING, 0.0f },
    { TASK_SET_ACTIVITY, (float)ACT_IDLE },
    { TASK_WAIT, 2.0f },
    { TASK_TLK_HEADRESET, 0.0f },
};

Schedule_t slIdleSciStand[] =
{
    {
        tlIdleSciStand,
        ARRAYSIZE(tlIdleSciStand),
        bits_COND_NEW_ENEMY | bits_COND_LIGHT_DAMAGE |
        bits_COND_HEAVY_DAMAGE | bits_COND_HEAR_SOUND |
        bits_COND_SMELL | bits_COND_CLIENT_PUSH |
        bits_COND_PROVOKED,
        bits_SOUND_COMBAT | bits_SOUND_DANGER |
        bits_SOUND_MEAT | bits_SOUND_CARCASS |
        bits_SOUND_GARBAGE,
        "IdleSciStand"
    },
};

// Cover task list
Task_t tlScientistCover[] =
{
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },
    { TASK_STOP_MOVING, 0.0f },
    { TASK_FIND_COVER_FROM_ENEMY, 0.0f },
    { TASK_RUN_PATH_SCARED, 0.0f },
    { TASK_TURN_LEFT, 179.0f },
    { TASK_SET_SCHEDULE, (float)SCHED_HIDE },
};

Schedule_t slScientistCover[] =
{
    {
        tlScientistCover,
        ARRAYSIZE(tlScientistCover),
        bits_COND_NEW_ENEMY,
        0,
        "ScientistCover"
    },
};

// Hide task list
Task_t tlScientistHide[] =
{
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },
    { TASK_STOP_MOVING, 0.0f },
    { TASK_PLAY_SEQUENCE, (float)ACT_CROUCH },
    { TASK_SET_ACTIVITY, (float)ACT_CROUCHIDLE },
    { TASK_WAIT_RANDOM, 10.0f },
};

Schedule_t slScientistHide[] =
{
    {
        tlScientistHide,
        ARRAYSIZE(tlScientistHide),
        bits_COND_NEW_ENEMY | bits_COND_HEAR_SOUND |
        bits_COND_SEE_ENEMY | bits_COND_SEE_HATE |
        bits_COND_SEE_FEAR | bits_COND_SEE_DISLIKE,
        bits_SOUND_DANGER,
        "ScientistHide"
    },
};

// Startle task list
Task_t tlScientistStartle[] =
{
    { TASK_SET_FAIL_SCHEDULE, (float)SCHED_PANIC },
    { TASK_RANDOM_SCREAM, 0.3f },
    { TASK_STOP_MOVING, 0.0f },
    { TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_CROUCH },
    { TASK_RANDOM_SCREAM, 0.1f },
    { TASK_PLAY_SEQUENCE_FACE_ENEMY, (float)ACT_CROUCHIDLE },
    { TASK_WAIT_RANDOM, 1.0f },
};

Schedule_t slScientistStartle[] =
{
    {
        tlScientistStartle,
        ARRAYSIZE(tlScientistStartle),
        bits_COND_NEW_ENEMY | bits_COND_SEE_ENEMY |
        bits_COND_SEE_HATE | bits_COND_SEE_FEAR |
        bits_COND_SEE_DISLIKE,
        0,
        "ScientistStartle"
    },
};

// Fear task list
Task_t tlFear[] =
{
    { TASK_STOP_MOVING, 0.0f },
    { TASK_FACE_ENEMY, 0.0f },
    { TASK_SAY_FEAR, 0.0f },
};

Schedule_t slFear[] =
{
    {
        tlFear,
        ARRAYSIZE(tlFear),
        bits_COND_NEW_ENEMY,
        0,
        "Fear"
    },
};

DEFINE_CUSTOM_SCHEDULES(CScientist)
{
    slFollow,
        slFaceTarget,
        slIdleSciStand,
        slFear,
        slScientistCover,
        slScientistHide,
        slScientistStartle,
        slHeal,
        slStopFollowing,
        slSciPanic,
        slFollowScared,
        slFaceTargetScared,
};

IMPLEMENT_CUSTOM_SCHEDULES(CScientist, CTalkMonster);

//=========================================================
// CScientist Implementation
//=========================================================

char* CScientist::GetScientistModel(void) const
{
    char* pszOverride = (char*)CVAR_GET_STRING("_sv_override_scientist_mdl");
    if (pszOverride && strlen(pszOverride) > 5) // at least requires ".mdl"
    {
        return pszOverride;
    }
    return "models/scientist.mdl";
}

void CScientist::DeclineFollowing(void)
{
    Talk(10);
    m_hTalkTarget = m_hEnemy;
    PlaySentence("SC_POK", 2, VOL_NORM, ATTN_NORM);
}

void CScientist::Scream(void)
{
    if (FOkToSpeak())
    {
        Talk(10);
        m_hTalkTarget = m_hEnemy;
        PlaySentence("SC_SCREAM", RANDOM_FLOAT(3.0f, 6.0f), VOL_NORM, ATTN_NORM);
    }
}

Activity CScientist::GetStoppedActivity(void)
{
    return (m_hEnemy != NULL) ? ACT_EXCITED : CTalkMonster::GetStoppedActivity();
}

void CScientist::StartTask(Task_t* pTask)
{
    switch (pTask->iTask)
    {
    case TASK_SAY_HEAL:
        Talk(2);
        m_hTalkTarget = m_hTargetEnt;
        PlaySentence("SC_HEAL", 2, VOL_NORM, ATTN_IDLE);
        TaskComplete();
        break;

    case TASK_SCREAM:
        Scream();
        TaskComplete();
        break;

    case TASK_RANDOM_SCREAM:
        if (RANDOM_FLOAT(0.0f, 1.0f) < pTask->flData)
        {
            Scream();
        }
        TaskComplete();
        break;

    case TASK_SAY_FEAR:
        if (FOkToSpeak())
        {
            Talk(2);
            m_hTalkTarget = m_hEnemy;
            if (m_hEnemy->IsPlayer())
            {
                PlaySentence("SC_PLFEAR", 5, VOL_NORM, ATTN_NORM);
            }
            else
            {
                PlaySentence("SC_FEAR", 5, VOL_NORM, ATTN_NORM);
            }
        }
        TaskComplete();
        break;

    case TASK_HEAL:
        m_IdealActivity = ACT_MELEE_ATTACK1;
        break;

    case TASK_RUN_PATH_SCARED:
        m_movementActivity = ACT_RUN_SCARED;
        break;

    case TASK_MOVE_TO_TARGET_RANGE_SCARED:
    {
        float distance = (m_hTargetEnt->pev->origin - pev->origin).Length();
        if (distance < 1.0f)
        {
            TaskComplete();
        }
        else
        {
            m_vecMoveGoal = m_hTargetEnt->pev->origin;
            if (!MoveToTarget(ACT_WALK_SCARED, 0.5f))
            {
                TaskFail();
            }
        }
    }
    break;

    default:
        CTalkMonster::StartTask(pTask);
        break;
    }
}

void CScientist::RunTask(Task_t* pTask)
{
    switch (pTask->iTask)
    {
    case TASK_RUN_PATH_SCARED:
        if (MovementIsComplete())
        {
            TaskComplete();
        }
        if (RANDOM_LONG(0, 31) < 8)
        {
            Scream();
        }
        break;

    case TASK_MOVE_TO_TARGET_RANGE_SCARED:
    {
        if (RANDOM_LONG(0, 63) < 8)
        {
            Scream();
        }

        if (m_hEnemy == NULL)
        {
            TaskFail();
            break;
        }

        float distance = (m_vecMoveGoal - pev->origin).Length2D();
        float targetRange = pTask->flData;

        // Re-evaluate when finished or target moved too far
        if ((distance < targetRange) ||
            (m_vecMoveGoal - m_hTargetEnt->pev->origin).Length() > targetRange * 0.5f)
        {
            m_vecMoveGoal = m_hTargetEnt->pev->origin;
            distance = (m_vecMoveGoal - pev->origin).Length2D();
            FRefreshRoute();
        }

        // Set appropriate activity based on distance
        if (distance < targetRange)
        {
            TaskComplete();
            RouteClear();
        }
        else if (distance < 190.0f && m_movementActivity != ACT_WALK_SCARED)
        {
            m_movementActivity = ACT_WALK_SCARED;
        }
        else if (distance >= 270.0f && m_movementActivity != ACT_RUN_SCARED)
        {
            m_movementActivity = ACT_RUN_SCARED;
        }
    }
    break;

    case TASK_HEAL:
        if (m_fSequenceFinished)
        {
            TaskComplete();
        }
        else
        {
            if (TargetDistance() > 90.0f)
            {
                TaskComplete();
            }
            pev->ideal_yaw = UTIL_VecToYaw(m_hTargetEnt->pev->origin - pev->origin);
            ChangeYaw(pev->yaw_speed);
        }
        break;

    default:
        CTalkMonster::RunTask(pTask);
        break;
    }
}

int CScientist::Classify(void)
{
    return CLASS_HUMAN_PASSIVE;
}

void CScientist::SetYawSpeed(void)
{
    int ys = 90;

    switch (m_Activity)
    {
    case ACT_IDLE:          ys = 120; break;
    case ACT_WALK:          ys = 180; break;
    case ACT_RUN:           ys = 150; break;
    case ACT_TURN_LEFT:
    case ACT_TURN_RIGHT:    ys = 120; break;
    default:                break;
    }

    pev->yaw_speed = ys;
}

void CScientist::HandleAnimEvent(MonsterEvent_t* pEvent)
{
    switch (pEvent->event)
    {
    case SCIENTIST_AE_HEAL:
        Heal();
        break;

    case SCIENTIST_AE_NEEDLEON:
    {
        int oldBody = pev->body;
        pev->body = (oldBody % NUM_SCIENTIST_HEADS) + NUM_SCIENTIST_HEADS * 1;
    }
    break;

    case SCIENTIST_AE_NEEDLEOFF:
    {
        int oldBody = pev->body;
        pev->body = (oldBody % NUM_SCIENTIST_HEADS) + NUM_SCIENTIST_HEADS * 0;
    }
    break;

    default:
        CTalkMonster::HandleAnimEvent(pEvent);
        break;
    }
}

void CScientist::Spawn(void)
{
    Precache();

    SET_MODEL(ENT(pev), GetScientistModel());
    UTIL_SetSize(pev, VEC_HUMAN_HULL_MIN, VEC_HUMAN_HULL_MAX);

    pev->solid = SOLID_SLIDEBOX;
    pev->movetype = MOVETYPE_STEP;
    m_bloodColor = BLOOD_COLOR_RED;
    pev->health = gSkillData.scientistHealth;
    pev->view_ofs = Vector(0, 0, 50);
    m_flFieldOfView = VIEW_FIELD_WIDE;
    m_MonsterState = MONSTERSTATE_NONE;
    m_afCapability = bits_CAP_HEAR | bits_CAP_TURN_HEAD | bits_CAP_OPEN_DOORS |
        bits_CAP_AUTO_DOORS | bits_CAP_USE;

    // White hands
    pev->skin = 0;

    if (pev->body == -1)
    {
        pev->body = RANDOM_LONG(0, NUM_SCIENTIST_HEADS - 1);
    }

    // Luther is black, make his hands black
    if (pev->body == HEAD_LUTHER)
    {
        pev->skin = 1;
    }

    MonsterInit();
    SetUse(&CScientist::FollowerUse);
}

void CScientist::Precache(void)
{
    PRECACHE_MODEL(GetScientistModel());
    PRECACHE_SOUND("scientist/sci_pain1.wav");
    PRECACHE_SOUND("scientist/sci_pain2.wav");
    PRECACHE_SOUND("scientist/sci_pain3.wav");
    PRECACHE_SOUND("scientist/sci_pain4.wav");
    PRECACHE_SOUND("scientist/sci_pain5.wav");

    TalkInit();
    CTalkMonster::Precache();
}

void CScientist::TalkInit(void)
{
    CTalkMonster::TalkInit();

    // Scientist will try to talk to friends in this order:
    m_szFriends[0] = "monster_scientist";
    m_szFriends[1] = "monster_sitting_scientist";
    m_szFriends[2] = "monster_barney";

    // Scientists speech group names (group names are in sentences.txt)
    m_szGrp[TLK_ANSWER] = "SC_ANSWER";
    m_szGrp[TLK_QUESTION] = "SC_QUESTION";
    m_szGrp[TLK_IDLE] = "SC_IDLE";
    m_szGrp[TLK_STARE] = "SC_STARE";
    m_szGrp[TLK_USE] = "SC_OK";
    m_szGrp[TLK_UNUSE] = "SC_WAIT";
    m_szGrp[TLK_STOP] = "SC_STOP";
    m_szGrp[TLK_NOSHOOT] = "SC_SCARED";
    m_szGrp[TLK_HELLO] = "SC_HELLO";
    m_szGrp[TLK_PLHURT1] = "!SC_CUREA";
    m_szGrp[TLK_PLHURT2] = "!SC_CUREB";
    m_szGrp[TLK_PLHURT3] = "!SC_CUREC";
    m_szGrp[TLK_PHELLO] = "SC_PHELLO";
    m_szGrp[TLK_PIDLE] = "SC_PIDLE";
    m_szGrp[TLK_PQUESTION] = "SC_PQUEST";
    m_szGrp[TLK_SMELL] = "SC_SMELL";
    m_szGrp[TLK_WOUND] = "SC_WOUND";
    m_szGrp[TLK_MORTAL] = "SC_MORTAL";

    // Get voice for head
    switch (pev->body % 3)
    {
    default:
    case HEAD_GLASSES:  m_voicePitch = 105; break;
    case HEAD_EINSTEIN: m_voicePitch = 100; break;
    case HEAD_LUTHER:   m_voicePitch = 95;  break;
    case HEAD_SLICK:    m_voicePitch = 100; break;
    }
}

int CScientist::TakeDamage(entvars_t* pevInflictor, entvars_t* pevAttacker,
    float flDamage, int bitsDamageType)
{
    if (pevInflictor && (pevInflictor->flags & FL_CLIENT))
    {
        Remember(bits_MEMORY_PROVOKED);
        StopFollowing(TRUE);
    }

    return CTalkMonster::TakeDamage(pevInflictor, pevAttacker, flDamage, bitsDamageType);
}

int CScientist::ISoundMask(void)
{
    return bits_SOUND_WORLD | bits_SOUND_COMBAT | bits_SOUND_DANGER | bits_SOUND_PLAYER;
}

void CScientist::PainSound(void)
{
    if (gpGlobals->time < m_painTime)
    {
        return;
    }

    m_painTime = gpGlobals->time + RANDOM_FLOAT(0.5f, 0.75f);

    const char* soundFiles[] = {
        "scientist/sci_pain1.wav",
        "scientist/sci_pain2.wav",
        "scientist/sci_pain3.wav",
        "scientist/sci_pain4.wav",
        "scientist/sci_pain5.wav"
    };

    int index = RANDOM_LONG(0, 4);
    EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, soundFiles[index], 1.0f, ATTN_NORM, 0, GetVoicePitch());
}

void CScientist::DeathSound(void)
{
    PainSound();
}

void CScientist::Killed(entvars_t* pevAttacker, int iGib)
{
    SetUse(NULL);
    CTalkMonster::Killed(pevAttacker, iGib);
}

void CScientist::SetActivity(Activity newActivity)
{
    int iSequence = LookupActivity(newActivity);

    if (iSequence == ACTIVITY_NOT_AVAILABLE)
    {
        newActivity = ACT_IDLE;
    }
    CTalkMonster::SetActivity(newActivity);
}

Schedule_t* CScientist::GetScheduleOfType(int Type)
{
    Schedule_t* psched;

    switch (Type)
    {
        // Hook these to make a looping schedule
    case SCHED_TARGET_FACE:
        psched = CTalkMonster::GetScheduleOfType(Type);
        if (psched == slIdleStand)
        {
            return slFaceTarget;
        }
        return psched;

    case SCHED_TARGET_CHASE:
        return slFollow;

    case SCHED_CANT_FOLLOW:
        return slStopFollowing;

    case SCHED_PANIC:
        return slSciPanic;

    case SCHED_TARGET_CHASE_SCARED:
        return slFollowScared;

    case SCHED_TARGET_FACE_SCARED:
        return slFaceTargetScared;

    case SCHED_IDLE_STAND:
        psched = CTalkMonster::GetScheduleOfType(Type);
        if (psched == slIdleStand)
        {
            return slIdleSciStand;
        }
        return psched;

    case SCHED_HIDE:
        return slScientistHide;

    case SCHED_STARTLE:
        return slScientistStartle;

    case SCHED_FEAR:
        return slFear;
    }

    return CTalkMonster::GetScheduleOfType(Type);
}

Schedule_t* CScientist::GetSchedule(void)
{
    // Handle dangerous sounds first
    if (HasConditions(bits_COND_HEAR_SOUND))
    {
        CSound* pSound = PBestSound();
        ASSERT(pSound != NULL);
        if (pSound && (pSound->m_iType & bits_SOUND_DANGER))
        {
            return GetScheduleOfType(SCHED_TAKE_COVER_FROM_BEST_SOUND);
        }
    }

    switch (m_MonsterState)
    {
    case MONSTERSTATE_ALERT:
    case MONSTERSTATE_IDLE:
    {
        CBaseEntity* pEnemy = m_hEnemy;

        // Update fear timer
        if (pEnemy)
        {
            if (HasConditions(bits_COND_SEE_ENEMY))
            {
                m_fearTime = gpGlobals->time;
            }
            else if (DisregardEnemy(pEnemy))
            {
                m_hEnemy = NULL;
                pEnemy = NULL;
            }
        }

        // React to damage
        if (HasConditions(bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE))
        {
            return GetScheduleOfType(SCHED_SMALL_FLINCH);
        }

        // React to scary sounds
        if (HasConditions(bits_COND_HEAR_SOUND))
        {
            CSound* pSound = PBestSound();
            ASSERT(pSound != NULL);
            if (pSound && (pSound->m_iType & (bits_SOUND_DANGER | bits_SOUND_COMBAT)))
            {
                if (gpGlobals->time - m_fearTime > 3.0f)
                {
                    m_fearTime = gpGlobals->time;
                    return GetScheduleOfType(SCHED_STARTLE);
                }
            }
        }

        // Following behavior
        if (IsFollowing())
        {
            if (!m_hTargetEnt->IsAlive())
            {
                StopFollowing(FALSE);
                break;
            }

            int relationship = R_NO;
            if (pEnemy != NULL)
            {
                relationship = IRelationship(pEnemy);
            }

            // Check if afraid
            if (relationship != R_DL && relationship != R_HT)
            {
                // Close enough to target
                if (TargetDistance() <= 128.0f)
                {
                    if (CanHeal())
                    {
                        return slHeal;
                    }
                    if (HasConditions(bits_COND_CLIENT_PUSH))
                    {
                        return GetScheduleOfType(SCHED_MOVE_AWAY_FOLLOW);
                    }
                }
                return GetScheduleOfType(SCHED_TARGET_FACE);
            }
            else // Scared behavior
            {
                if (HasConditions(bits_COND_NEW_ENEMY))
                {
                    return GetScheduleOfType(SCHED_FEAR);
                }
                return GetScheduleOfType(SCHED_TARGET_FACE_SCARED);
            }
        }

        if (HasConditions(bits_COND_CLIENT_PUSH))
        {
            return GetScheduleOfType(SCHED_MOVE_AWAY);
        }

        TrySmellTalk();
        break;
    }

    case MONSTERSTATE_COMBAT:
        if (HasConditions(bits_COND_NEW_ENEMY))
        {
            return slFear;
        }
        if (HasConditions(bits_COND_SEE_ENEMY))
        {
            return slScientistCover;
        }
        if (HasConditions(bits_COND_HEAR_SOUND))
        {
            return slTakeCoverFromBestSound;
        }
        return slScientistCover;
    }

    return CTalkMonster::GetSchedule();
}

MONSTERSTATE CScientist::GetIdealState(void)
{
    switch (m_MonsterState)
    {
    case MONSTERSTATE_ALERT:
    case MONSTERSTATE_IDLE:
        if (HasConditions(bits_COND_NEW_ENEMY))
        {
            if (IsFollowing())
            {
                int relationship = IRelationship(m_hEnemy);
                if (relationship != R_FR && relationship != R_HT &&
                    !HasConditions(bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE))
                {
                    m_IdealMonsterState = MONSTERSTATE_ALERT;
                    return m_IdealMonsterState;
                }
                StopFollowing(TRUE);
            }
        }
        else if (HasConditions(bits_COND_LIGHT_DAMAGE | bits_COND_HEAVY_DAMAGE))
        {
            if (IsFollowing())
            {
                StopFollowing(TRUE);
            }
        }
        break;

    case MONSTERSTATE_COMBAT:
    {
        CBaseEntity* pEnemy = m_hEnemy;
        if (pEnemy != NULL)
        {
            if (DisregardEnemy(pEnemy))
            {
                m_IdealMonsterState = MONSTERSTATE_ALERT;
                m_hEnemy = NULL;
                return m_IdealMonsterState;
            }

            if (m_hTargetEnt != NULL)
            {
                m_IdealMonsterState = MONSTERSTATE_ALERT;
                return m_IdealMonsterState;
            }

            if (HasConditions(bits_COND_SEE_ENEMY))
            {
                m_fearTime = gpGlobals->time;
                m_IdealMonsterState = MONSTERSTATE_COMBAT;
                return m_IdealMonsterState;
            }
        }
    }
    break;

    default:
        break;
    }

    return CTalkMonster::GetIdealState();
}

BOOL CScientist::CanHeal(void)
{
    if (m_healTime > gpGlobals->time || m_hTargetEnt == NULL)
    {
        return FALSE;
    }
    return (m_hTargetEnt->pev->health > (m_hTargetEnt->pev->max_health * 0.5f));
}

void CScientist::Heal(void)
{
    if (!CanHeal())
    {
        return;
    }

    Vector target = m_hTargetEnt->pev->origin - pev->origin;
    if (target.Length() > 100.0f)
    {
        return;
    }

    m_hTargetEnt->TakeHealth(gSkillData.scientistHeal, DMG_GENERIC);
    m_healTime = gpGlobals->time + 60.0f; // Don't heal again for 1 minute
}

int CScientist::FriendNumber(int arrayNumber)
{
    static int array[3] = { 1, 2, 0 };
    if (arrayNumber < 3)
    {
        return array[arrayNumber];
    }
    return arrayNumber;
}

//=========================================================
// CDeadScientist - Dead scientist prop
//=========================================================

class CDeadScientist : public CBaseMonster
{
public:
    void Spawn(void);
    int Classify(void) { return CLASS_HUMAN_PASSIVE; }

    char* GetScientistModel(void) const;
    void KeyValue(KeyValueData* pkvd);

private:
    int m_iPose;
    static char* m_szPoses[7];
};

char* CDeadScientist::m_szPoses[7] =
{
    "lying_on_back",
    "lying_on_stomach",
    "dead_sitting",
    "dead_hang",
    "dead_table1",
    "dead_table2",
    "dead_table3"
};

LINK_ENTITY_TO_CLASS(monster_scientist_dead, CDeadScientist);

char* CDeadScientist::GetScientistModel(void) const
{
    char* pszOverride = (char*)CVAR_GET_STRING("_sv_override_scientist_mdl");
    if (pszOverride && strlen(pszOverride) > 5)
    {
        return pszOverride;
    }
    return "models/scientist.mdl";
}

void CDeadScientist::KeyValue(KeyValueData* pkvd)
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

void CDeadScientist::Spawn(void)
{
    PRECACHE_MODEL(GetScientistModel());
    SET_MODEL(ENT(pev), GetScientistModel());

    pev->effects = 0;
    pev->sequence = 0;
    pev->health = 8.0f;
    m_bloodColor = BLOOD_COLOR_RED;

    if (pev->body == -1)
    {
        pev->body = RANDOM_LONG(0, NUM_SCIENTIST_HEADS - 1);
    }

    // Luther is black, make his hands black
    if (pev->body == HEAD_LUTHER)
    {
        pev->skin = 1;
    }
    else
    {
        pev->skin = 0;
    }

    pev->sequence = LookupSequence(m_szPoses[m_iPose]);
    if (pev->sequence == -1)
    {
        ALERT(at_console, "Dead scientist with bad pose\n");
    }

    MonsterInitDead();
}

//=========================================================
// CSittingScientist - Sitting scientist prop with speech
//=========================================================

class CSittingScientist : public CScientist
{
public:
    void Spawn(void);
    void Precache(void);

    void EXPORT SittingThink(void);
    int Classify(void);

    int Save(CSave& save);
    int Restore(CRestore& restore);
    static TYPEDESCRIPTION m_SaveData[];

    void SetAnswerQuestion(CTalkMonster* pSpeaker);
    int FriendNumber(int arrayNumber);

    int FIdleSpeak(void);

private:
    int m_baseSequence;
    int m_headTurn;
    float m_flResponseDelay;
};

LINK_ENTITY_TO_CLASS(monster_sitting_scientist, CSittingScientist);

TYPEDESCRIPTION CSittingScientist::m_SaveData[] =
{
    DEFINE_FIELD(CSittingScientist, m_headTurn, FIELD_INTEGER),
    DEFINE_FIELD(CSittingScientist, m_flResponseDelay, FIELD_FLOAT),
};

IMPLEMENT_SAVERESTORE(CSittingScientist, CScientist);

// Animation sequence aliases 
enum
{
    SITTING_ANIM_sitlookleft,
    SITTING_ANIM_sitlookright,
    SITTING_ANIM_sitscared,
    SITTING_ANIM_sitting2,
    SITTING_ANIM_sitting3
};

void CSittingScientist::Spawn(void)
{
    PRECACHE_MODEL(GetScientistModel());
    SET_MODEL(ENT(pev), GetScientistModel());
    Precache();
    InitBoneControllers();

    UTIL_SetSize(pev, Vector(-14, -14, 0), Vector(14, 14, 36));

    pev->solid = SOLID_SLIDEBOX;
    pev->movetype = MOVETYPE_STEP;
    pev->effects = 0;
    pev->health = 50.0f;
    m_bloodColor = BLOOD_COLOR_RED;
    m_flFieldOfView = VIEW_FIELD_WIDE;
    m_afCapability = bits_CAP_HEAR | bits_CAP_TURN_HEAD;

    SetBits(pev->spawnflags, SF_MONSTER_PREDISASTER);

    if (pev->body == -1)
    {
        pev->body = RANDOM_LONG(0, NUM_SCIENTIST_HEADS - 1);
    }

    // Luther is black, make his hands black
    if (pev->body == HEAD_LUTHER)
    {
        pev->skin = 1;
    }

    m_baseSequence = LookupSequence("sitlookleft");
    pev->sequence = m_baseSequence + RANDOM_LONG(0, 4);
    ResetSequenceInfo();

    SetThink(&CSittingScientist::SittingThink);
    pev->nextthink = gpGlobals->time + 0.1f;

    DROP_TO_FLOOR(ENT(pev));
}

void CSittingScientist::Precache(void)
{
    m_baseSequence = LookupSequence("sitlookleft");
    TalkInit();
}

int CSittingScientist::Classify(void)
{
    return CLASS_HUMAN_PASSIVE;
}

int CSittingScientist::FriendNumber(int arrayNumber)
{
    static int array[3] = { 2, 1, 0 };
    if (arrayNumber < 3)
    {
        return array[arrayNumber];
    }
    return arrayNumber;
}

void CSittingScientist::SittingThink(void)
{
    CBaseEntity* pent;

    StudioFrameAdvance();

    // Try to greet player
    if (FIdleHello())
    {
        pent = FindNearestFriend(TRUE);
        if (pent)
        {
            float yaw = VecToYaw(pent->pev->origin - pev->origin) - pev->angles.y;
            if (yaw > 180.0f) yaw -= 360.0f;
            if (yaw < -180.0f) yaw += 360.0f;

            if (yaw > 0)
            {
                pev->sequence = m_baseSequence + SITTING_ANIM_sitlookleft;
            }
            else
            {
                pev->sequence = m_baseSequence + SITTING_ANIM_sitlookright;
            }

            ResetSequenceInfo();
            pev->frame = 0;
            SetBoneController(0, 0);
        }
    }
    else if (m_fSequenceFinished)
    {
        int i = RANDOM_LONG(0, 99);
        m_headTurn = 0;

        if (m_flResponseDelay > 0.0f && gpGlobals->time > m_flResponseDelay)
        {
            // Respond to question
            IdleRespond();
            pev->sequence = m_baseSequence + SITTING_ANIM_sitscared;
            m_flResponseDelay = 0.0f;
        }
        else if (i < 30)
        {
            pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;

            // Turn towards player or nearest friend and speak
            CBaseEntity* pentFriend;
            if (!FBitSet(m_bitsSaid, bit_saidHelloPlayer))
            {
                pentFriend = FindNearestFriend(TRUE);
            }
            else
            {
                pentFriend = FindNearestFriend(FALSE);
            }

            if (!FIdleSpeak() || !pentFriend)
            {
                m_headTurn = RANDOM_LONG(0, 8) * 10 - 40;
                pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;
            }
            else
            {
                // Only turn head if we spoke
                float yaw = VecToYaw(pentFriend->pev->origin - pev->origin) - pev->angles.y;
                if (yaw > 180.0f) yaw -= 360.0f;
                if (yaw < -180.0f) yaw += 360.0f;

                if (yaw > 0)
                {
                    pev->sequence = m_baseSequence + SITTING_ANIM_sitlookleft;
                }
                else
                {
                    pev->sequence = m_baseSequence + SITTING_ANIM_sitlookright;
                }
            }
        }
        else if (i < 60)
        {
            pev->sequence = m_baseSequence + SITTING_ANIM_sitting3;
            m_headTurn = RANDOM_LONG(0, 8) * 10 - 40;
            if (RANDOM_LONG(0, 99) < 5)
            {
                FIdleSpeak();
            }
        }
        else if (i < 80)
        {
            pev->sequence = m_baseSequence + SITTING_ANIM_sitting2;
        }
        else
        {
            pev->sequence = m_baseSequence + SITTING_ANIM_sitscared;
        }

        ResetSequenceInfo();
        pev->frame = 0;
        SetBoneController(0, m_headTurn);
    }

    pev->nextthink = gpGlobals->time + 0.1f;
}

void CSittingScientist::SetAnswerQuestion(CTalkMonster* pSpeaker)
{
    m_flResponseDelay = gpGlobals->time + RANDOM_FLOAT(3.0f, 4.0f);
    m_hTalkTarget = (CBaseMonster*)pSpeaker;
}

int CSittingScientist::FIdleSpeak(void)
{
    if (!FOkToSpeak())
    {
        return FALSE;
    }

    CTalkMonster::g_talkWaitTime = gpGlobals->time + RANDOM_FLOAT(4.8f, 5.2f);
    int pitch = GetVoicePitch();

    // Try to talk to any standing or sitting scientists nearby
    CBaseEntity* pentFriend = FindNearestFriend(FALSE);

    if (pentFriend && RANDOM_LONG(0, 1))
    {
        CTalkMonster* pTalkMonster = (CTalkMonster*)(CBaseEntity*)pentFriend;
        pTalkMonster->SetAnswerQuestion(this);

        IdleHeadTurn(pentFriend->pev->origin);
        SENTENCEG_PlayRndSz(ENT(pev), m_szGrp[TLK_PQUESTION], 1.0f, ATTN_IDLE, 0, pitch);
        CTalkMonster::g_talkWaitTime = gpGlobals->time + RANDOM_FLOAT(4.8f, 5.2f);
        return TRUE;
    }

    // Play idle statement
    if (RANDOM_LONG(0, 1))
    {
        SENTENCEG_PlayRndSz(ENT(pev), m_szGrp[TLK_PIDLE], 1.0f, ATTN_IDLE, 0, pitch);
        CTalkMonster::g_talkWaitTime = gpGlobals->time + RANDOM_FLOAT(4.8f, 5.2f);
        return TRUE;
    }

    CTalkMonster::g_talkWaitTime = 0.0f;
    return FALSE;
}