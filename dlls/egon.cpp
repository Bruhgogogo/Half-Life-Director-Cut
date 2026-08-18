/***
*
*	Copyright (c) 1996-2001, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
#if !defined( OEM_BUILD ) && !defined( HLDEMO_BUILD )

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "player.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "effects.h"
#include "customentity.h"
#include "gamerules.h"

// Sprite constants
constexpr char* EGON_BEAM_SPRITE = "sprites/xbeam1.spr";
constexpr char* EGON_FLARE_SPRITE = "sprites/XSpark1.spr";

// Sound constants
constexpr char* EGON_SOUND_OFF = "weapons/egon_off1.wav";
constexpr char* EGON_SOUND_RUN = "weapons/egon_run3.wav";
constexpr char* EGON_SOUND_STARTUP = "weapons/egon_windup2.wav";
constexpr char* EGON_SOUND_COCK = "weapons/357_cock1.wav";

// Combat constants
constexpr int EGON_PRIMARY_VOLUME = 450;
constexpr float EGON_RANGE = 2048.0f;
constexpr float EGON_PULSE_INTERVAL = 0.1f;
constexpr float EGON_DISCHARGE_INTERVAL = 0.1f;
constexpr float EGON_AMMO_USE_MP_NARROW = 0.1f;      // 10 per second
constexpr float EGON_AMMO_USE_SP_NARROW = 0.166f;    // 6 per second
constexpr float EGON_AMMO_USE_MP_WIDE = 0.2f;        // 5 per second
constexpr float EGON_AMMO_USE_SP_WIDE = 0.1f;        // 10 per second
constexpr float EGON_SHAKE_INTERVAL = 1.5f;
constexpr float EGON_HOLSTER_DELAY = 0.5f;
constexpr float EGON_IDLE_DELAY_MIN = 10.0f;
constexpr float EGON_IDLE_DELAY_MAX = 15.0f;
constexpr float EGON_FIDGET_DELAY = 3.0f;
constexpr float EGON_IDLE_RESET_DELAY = 2.0f;
constexpr float EGON_ATTACK_COOLDOWN = 0.5f;
constexpr float EGON_EMPTY_SOUND_DELAY = 0.25f;
constexpr float EGON_SWITCH_DELAY = 0.2f;
constexpr float EGON_CHARGE_TIME = 2.0f;

// Beam constants
constexpr int BEAM_WIDTH_MAX = 40;
constexpr int BEAM_WIDTH_MIN = 20;
constexpr int BEAM_WIDTH_NARROW = 20;
constexpr int BEAM_BRIGHTNESS_MAX = 255;
constexpr int BEAM_BRIGHTNESS_MIN = 75;
constexpr int BEAM_NOISE_WIDE = 20;
constexpr int BEAM_NOISE_NARROW = 5;
constexpr int BEAM_SCROLL_WIDE = 50;
constexpr int BEAM_SCROLL_NARROW = 110;
constexpr int BEAM_NOISE_SUB_WIDE = 8;
constexpr int BEAM_NOISE_SUB_NARROW = 2;

// Color constants (R, G, B)
constexpr int COLOR_R_BASE = 30;
constexpr int COLOR_R_VARY = 25;
constexpr int COLOR_G_BASE = 120;
constexpr int COLOR_G_VARY = 30;
constexpr int COLOR_B_BASE_NARROW = 30;
constexpr int COLOR_B_VARY_NARROW = 30;
constexpr int COLOR_B_BASE_WIDE = 64;
constexpr int COLOR_B_VARY_WIDE = 80;
constexpr int COLOR_SUB_R = 50;
constexpr int COLOR_SUB_G = 50;
constexpr int COLOR_SUB_B = 255;

// Damage constants
constexpr float RADIUS_DAMAGE_RANGE = 128.0f;
constexpr float RADIUS_DAMAGE_MODIFIER = 0.25f;
constexpr int RADIUS_DAMAGE_AMOUNT = 128;
constexpr float SCREEN_SHAKE_AMPLITUDE = 5.0f;
constexpr float SCREEN_SHAKE_FREQUENCY = 150.0f;
constexpr float SCREEN_SHAKE_DURATION = 0.75f;
constexpr float SCREEN_SHAKE_RADIUS = 250.0f;

// Animation sequence IDs
enum EgonAnim : int
{
    EGON_IDLE1 = 0,
    EGON_FIDGET1,
    EGON_ALTFIREON,
    EGON_ALTFIRECYCLE,
    EGON_ALTFIREOFF,
    EGON_FIRE1,
    EGON_FIRE2,
    EGON_FIRE3,
    EGON_FIRE4,
    EGON_DRAW,
    EGON_HOLSTER
};

// Event precache constants
constexpr char* EGON_EVENT_FIRE = "events/egon_fire.sc";
constexpr char* EGON_EVENT_STOP = "events/egon_stop.sc";

#ifndef CLIENT_DLL
extern bool IsBustingGame();
#endif

LINK_ENTITY_TO_CLASS(weapon_egon, CEgon);

void CEgon::Spawn()
{
    Precache();
    m_iId = WEAPON_EGON;
    SET_MODEL(ENT(pev), "models/w_egon.mdl");

    m_iDefaultAmmo = EGON_DEFAULT_GIVE;
    m_fireMode = FIRE_NARROW;

    FallInit(); // get ready to fall down.
}

void CEgon::Precache()
{
    PRECACHE_MODEL("models/w_egon.mdl");
    PRECACHE_MODEL("models/v_egon.mdl");
    PRECACHE_MODEL("models/p_egon.mdl");

    PRECACHE_MODEL("models/w_9mmclip.mdl");
    PRECACHE_SOUND("items/9mmclip1.wav");

    PRECACHE_SOUND(EGON_SOUND_OFF);
    PRECACHE_SOUND(EGON_SOUND_RUN);
    PRECACHE_SOUND(EGON_SOUND_STARTUP);
    PRECACHE_SOUND(EGON_SOUND_COCK);

    PRECACHE_MODEL(EGON_BEAM_SPRITE);
    PRECACHE_MODEL(EGON_FLARE_SPRITE);

    m_usEgonFire = PRECACHE_EVENT(1, EGON_EVENT_FIRE);
    m_usEgonStop = PRECACHE_EVENT(1, EGON_EVENT_STOP);
}

BOOL CEgon::Deploy()
{
    m_deployed = FALSE;
    m_fireState = FIRE_OFF;
    return DefaultDeploy("models/v_egon.mdl", "models/p_egon.mdl", EGON_DRAW, "egon");
}

int CEgon::AddToPlayer(CBasePlayer* pPlayer)
{
    if (CBasePlayerWeapon::AddToPlayer(pPlayer))
    {
        MESSAGE_BEGIN(MSG_ONE, gmsgWeapPickup, nullptr, pPlayer->pev);
        WRITE_BYTE(m_iId);
        MESSAGE_END();
        return TRUE;
    }

    return FALSE;
}

void CEgon::Holster(int skiplocal /* = 0 */)
{
    m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + EGON_HOLSTER_DELAY;
    SendWeaponAnim(EGON_HOLSTER);

    EndAttack();
}

int CEgon::GetItemInfo(ItemInfo* p)
{
    p->pszName = STRING(pev->classname);
    p->pszAmmo1 = "uranium";
    p->iMaxAmmo1 = URANIUM_MAX_CARRY;
    p->pszAmmo2 = nullptr;
    p->iMaxAmmo2 = -1;
    p->iMaxClip = WEAPON_NOCLIP;
    p->iSlot = 3;
    p->iPosition = 2;
    p->iId = m_iId = WEAPON_EGON;
    p->iFlags = 0;
    p->iWeight = EGON_WEIGHT;

    return 1;
}

float CEgon::GetPulseInterval()
{
    return EGON_PULSE_INTERVAL;
}

float CEgon::GetDischargeInterval()
{
    return EGON_DISCHARGE_INTERVAL;
}

BOOL CEgon::HasAmmo()
{
    return (m_pPlayer->ammo_uranium > 0) ? TRUE : FALSE;
}

void CEgon::UseAmmo(int count)
{
#ifndef CLIENT_DLL
    if (IsBustingGame())
    {
        return;
    }
#endif

    if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] >= count)
    {
        m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] -= count;
    }
    else
    {
        m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] = 0;
    }
}

void CEgon::Attack()
{
    // Don't fire underwater
    if (m_pPlayer->pev->waterlevel == 3)
    {
        if (m_fireState != FIRE_OFF || m_pBeam != nullptr)
        {
            EndAttack();
        }
        else
        {
            PlayEmptySound();
        }
        return;
    }

    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);
    const Vector vecAiming = gpGlobals->v_forward;
    const Vector vecSrc = m_pPlayer->GetGunPosition();

#if defined(CLIENT_WEAPONS)
    const int eventFlags = FEV_NOTHOST;
#else
    const int eventFlags = 0;
#endif

    switch (m_fireState)
    {
    case FIRE_OFF:
    {
        if (!HasAmmo())
        {
            m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + EGON_EMPTY_SOUND_DELAY;
            PlayEmptySound();
            return;
        }

        m_flAmmoUseTime = gpGlobals->time; // start using ammo ASAP.

        PLAYBACK_EVENT_FULL(
            eventFlags,
            m_pPlayer->edict(),
            m_usEgonFire,
            0.0f,
            (float*)&g_vecZero,
            (float*)&g_vecZero,
            0.0f,
            0.0f,
            m_fireState,
            m_fireMode,
            1,
            0
        );

        m_shakeTime = 0.0f;

        m_pPlayer->m_iWeaponVolume = EGON_PRIMARY_VOLUME;
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.1f;
        pev->fuser1 = UTIL_WeaponTimeBase() + EGON_CHARGE_TIME;

        pev->dmgtime = gpGlobals->time + GetPulseInterval();
        m_fireState = FIRE_CHARGE;
        break;
    }

    case FIRE_CHARGE:
    {
        Fire(vecSrc, vecAiming);
        m_pPlayer->m_iWeaponVolume = EGON_PRIMARY_VOLUME;

        if (pev->fuser1 <= UTIL_WeaponTimeBase())
        {
            PLAYBACK_EVENT_FULL(
                eventFlags,
                m_pPlayer->edict(),
                m_usEgonFire,
                0.0f,
                (float*)&g_vecZero,
                (float*)&g_vecZero,
                0.0f,
                0.0f,
                m_fireState,
                m_fireMode,
                0,
                0
            );
            pev->fuser1 = 1000.0f;
        }

        if (!HasAmmo())
        {
            EndAttack();
            m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + 1.0f;
        }
        break;
    }
    }
}

void CEgon::SwitchFireMode()
{
    m_fireMode = (m_fireMode == FIRE_NARROW) ? FIRE_WIDE : FIRE_NARROW;

    EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, EGON_SOUND_COCK, 0.8f, ATTN_NORM);

    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + EGON_SWITCH_DELAY;
}

void CEgon::PrimaryAttack()
{
    Attack();
}

void CEgon::SecondaryAttack()
{
    SwitchFireMode();
}

void CEgon::Fire(const Vector& vecOrigSrc, const Vector& vecDir)
{
    const Vector vecDest = vecOrigSrc + vecDir * EGON_RANGE;
    TraceResult tr;

    const Vector tmpSrc = vecOrigSrc + gpGlobals->v_up * -8.0f + gpGlobals->v_right * 3.0f;

    UTIL_TraceLine(vecOrigSrc, vecDest, dont_ignore_monsters, m_pPlayer->edict(), &tr);

    if (tr.fAllSolid)
    {
        return;
    }

#ifndef CLIENT_DLL
    CBaseEntity* const pEntity = CBaseEntity::Instance(tr.pHit);

    if (pEntity == nullptr)
    {
        return;
    }

    if (g_pGameRules->IsMultiplayer())
    {
        if (m_pSprite && pEntity->pev->takedamage)
        {
            m_pSprite->pev->effects &= ~EF_NODRAW;
        }
        else if (m_pSprite)
        {
            m_pSprite->pev->effects |= EF_NODRAW;
        }
    }
#endif

    float timedist;

    switch (m_fireMode)
    {
    case FIRE_NARROW:
#ifndef CLIENT_DLL
        if (pev->dmgtime < gpGlobals->time)
        {
            // Narrow mode only does damage to the entity it hits
            ClearMultiDamage();
            if (pEntity->pev->takedamage)
            {
                pEntity->TraceAttack(m_pPlayer->pev, gSkillData.plrDmgEgonNarrow, vecDir, &tr, DMG_ENERGYBEAM);
            }
            ApplyMultiDamage(m_pPlayer->pev, m_pPlayer->pev);

            // Ammo consumption
            const float ammoInterval = g_pGameRules->IsMultiplayer() 
                ? EGON_AMMO_USE_MP_NARROW 
                : EGON_AMMO_USE_SP_NARROW;
            
            if (gpGlobals->time >= m_flAmmoUseTime)
            {
                UseAmmo(1);
                m_flAmmoUseTime = gpGlobals->time + ammoInterval;
            }

            pev->dmgtime = gpGlobals->time + GetPulseInterval();
        }
#endif
        timedist = (pev->dmgtime - gpGlobals->time) / GetPulseInterval();
        break;

    case FIRE_WIDE:
#ifndef CLIENT_DLL
        if (pev->dmgtime < gpGlobals->time)
        {
            // Wide mode does damage to the ent, and radius damage
            ClearMultiDamage();
            if (pEntity->pev->takedamage)
            {
                pEntity->TraceAttack(
                    m_pPlayer->pev,
                    gSkillData.plrDmgEgonWide,
                    vecDir,
                    &tr,
                    DMG_ENERGYBEAM | DMG_ALWAYSGIB
                );
            }
            ApplyMultiDamage(m_pPlayer->pev, m_pPlayer->pev);

            if (g_pGameRules->IsMultiplayer())
            {
                // Radius damage a little more potent in multiplayer
                ::RadiusDamage(
                    tr.vecEndPos,
                    pev,
                    m_pPlayer->pev,
                    gSkillData.plrDmgEgonWide * RADIUS_DAMAGE_MODIFIER,
                    RADIUS_DAMAGE_RANGE,
                    CLASS_NONE,
                    DMG_ENERGYBEAM | DMG_BLAST | DMG_ALWAYSGIB
                );
            }

            if (!m_pPlayer->IsAlive())
            {
                return;
            }

            // Ammo consumption
            const float ammoInterval = g_pGameRules->IsMultiplayer() 
                ? EGON_AMMO_USE_MP_WIDE 
                : EGON_AMMO_USE_SP_WIDE;
            
            if (gpGlobals->time >= m_flAmmoUseTime)
            {
                UseAmmo(1);
                m_flAmmoUseTime = gpGlobals->time + ammoInterval;
            }

            pev->dmgtime = gpGlobals->time + GetDischargeInterval();
            
            if (m_shakeTime < gpGlobals->time)
            {
                UTIL_ScreenShake(
                    tr.vecEndPos,
                    SCREEN_SHAKE_AMPLITUDE,
                    SCREEN_SHAKE_FREQUENCY,
                    SCREEN_SHAKE_DURATION,
                    SCREEN_SHAKE_RADIUS
                );
                m_shakeTime = gpGlobals->time + EGON_SHAKE_INTERVAL;
            }
        }
#endif
        timedist = (pev->dmgtime - gpGlobals->time) / GetDischargeInterval();
        break;
    }

    // Clamp timedist to [0, 1] range
    timedist = (timedist < 0.0f) ? 0.0f : ((timedist > 1.0f) ? 1.0f : timedist);
    timedist = 1.0f - timedist;

    UpdateEffect(tmpSrc, tr.vecEndPos, timedist);
}

void CEgon::UpdateEffect(const Vector& startPoint, const Vector& endPoint, float timeBlend)
{
#ifndef CLIENT_DLL
    if (m_pBeam == nullptr)
    {
        CreateEffect();
    }

    // Calculate beam properties based on timeBlend
    int beamBrightness = (BEAM_BRIGHTNESS_MAX - (timeBlend * 180.0f));
    int beamWidth = (BEAM_WIDTH_MAX - (timeBlend * 20.0f));

    // Color calculation
    const int beamR = (COLOR_R_BASE + (COLOR_R_VARY * timeBlend));
    const int beamG = (COLOR_G_BASE + (COLOR_G_VARY * timeBlend));
    
    int beamB;
    if (m_fireMode == FIRE_NARROW)
    {
        beamBrightness = (beamBrightness * 0.5f);
        beamWidth = (beamWidth * 0.5f);
        beamB = (COLOR_B_BASE_NARROW + (COLOR_B_VARY_NARROW * timeBlend));
    }
    else
    {
        beamB = (COLOR_B_BASE_WIDE + COLOR_B_VARY_WIDE * fabs(sin(gpGlobals->time * 10.0f)));
    }

    // Apply beam properties
    m_pBeam->SetStartPos(endPoint);
    m_pBeam->SetColor(beamR, beamG, beamB);
    m_pBeam->SetBrightness(beamBrightness);
    m_pBeam->SetWidth(beamWidth);

    // Update sprite
    UTIL_SetOrigin(m_pSprite->pev, endPoint);
    m_pSprite->pev->frame += 8.0f * gpGlobals->frametime;
    if (m_pSprite->pev->frame > m_pSprite->Frames())
    {
        m_pSprite->pev->frame = 0.0f;
    }

    m_pNoise->SetStartPos(endPoint);
#endif
}

void CEgon::CreateEffect()
{
#ifndef CLIENT_DLL
    DestroyEffect();

    // Create main beam
    m_pBeam = CBeam::BeamCreate(EGON_BEAM_SPRITE, BEAM_WIDTH_MAX);
    m_pBeam->PointEntInit(pev->origin, m_pPlayer->entindex());
    m_pBeam->SetFlags(BEAM_FSINE);
    m_pBeam->SetEndAttachment(1);
    m_pBeam->pev->spawnflags |= SF_BEAM_TEMPORARY;
    m_pBeam->pev->flags |= FL_SKIPLOCALHOST;
    m_pBeam->pev->owner = m_pPlayer->edict();

    // Create noise beam
    m_pNoise = CBeam::BeamCreate(EGON_BEAM_SPRITE, 55);
    m_pNoise->PointEntInit(pev->origin, m_pPlayer->entindex());
    m_pNoise->SetScrollRate(25);
    m_pNoise->SetBrightness(100);
    m_pNoise->SetEndAttachment(1);
    m_pNoise->pev->spawnflags |= SF_BEAM_TEMPORARY;
    m_pNoise->pev->flags |= FL_SKIPLOCALHOST;
    m_pNoise->pev->owner = m_pPlayer->edict();

    // Create flare sprite
    m_pSprite = CSprite::SpriteCreate(EGON_FLARE_SPRITE, pev->origin, FALSE);
    m_pSprite->pev->scale = 1.0f;
    m_pSprite->SetTransparency(kRenderGlow, 255, 255, 255, 255, kRenderFxNoDissipation);
    m_pSprite->pev->spawnflags |= SF_SPRITE_TEMPORARY;
    m_pSprite->pev->owner = m_pPlayer->edict();

    // Configure beam based on fire mode
    if (m_fireMode == FIRE_WIDE)
    {
        m_pBeam->SetScrollRate(BEAM_SCROLL_WIDE);
        m_pBeam->SetNoise(BEAM_NOISE_WIDE);
        m_pNoise->SetColor(COLOR_SUB_R, COLOR_SUB_G, COLOR_SUB_B);
        m_pNoise->SetNoise(BEAM_NOISE_SUB_WIDE);
    }
    else
    {
        m_pBeam->SetScrollRate(BEAM_SCROLL_NARROW);
        m_pBeam->SetNoise(BEAM_NOISE_NARROW);
        m_pNoise->SetColor(80, 120, 255);
        m_pNoise->SetNoise(BEAM_NOISE_SUB_NARROW);
    }
#endif
}

void CEgon::DestroyEffect()
{
#ifndef CLIENT_DLL
    if (m_pBeam != nullptr)
    {
        UTIL_Remove(m_pBeam);
        m_pBeam = nullptr;
    }
    
    if (m_pNoise != nullptr)
    {
        UTIL_Remove(m_pNoise);
        m_pNoise = nullptr;
    }
    
    if (m_pSprite != nullptr)
    {
        if (m_fireMode == FIRE_WIDE)
        {
            m_pSprite->Expand(10, 500);
        }
        else
        {
            UTIL_Remove(m_pSprite);
        }
        m_pSprite = nullptr;
    }
#endif
}

void CEgon::WeaponIdle()
{
    ResetEmptySound();

    if (m_flTimeWeaponIdle > gpGlobals->time)
    {
        return;
    }

    if (m_fireState != FIRE_OFF)
    {
        EndAttack();
    }

    const float flRand = RANDOM_FLOAT(0.0f, 1.0f);
    int iAnim;

    if (flRand <= 0.5f)
    {
        iAnim = EGON_IDLE1;
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 
            UTIL_SharedRandomFloat(m_pPlayer->random_seed, EGON_IDLE_DELAY_MIN, EGON_IDLE_DELAY_MAX);
    }
    else
    {
        iAnim = EGON_FIDGET1;
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + EGON_FIDGET_DELAY;
    }

    SendWeaponAnim(iAnim);
    m_deployed = TRUE;
}

BOOL CEgon::CanHolster()
{
#ifndef CLIENT_DLL
    if (IsBustingGame())
    {
        return FALSE;
    }
#endif

    return TRUE;
}

void CEgon::EndAttack()
{
    const bool bMakeNoise = (m_fireState != FIRE_OFF);

    PLAYBACK_EVENT_FULL(
        FEV_GLOBAL | FEV_RELIABLE,
        m_pPlayer->edict(),
        m_usEgonStop,
        0.0f,
        (float*)&m_pPlayer->pev->origin,
        (float*)&m_pPlayer->pev->angles,
        0.0f,
        0.0f,
        bMakeNoise ? 1 : 0,
        0,
        0,
        0
    );

    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + EGON_IDLE_RESET_DELAY;
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + EGON_ATTACK_COOLDOWN;

    m_fireState = FIRE_OFF;

    DestroyEffect();
}

//=========================================================
// Egon Ammo Entity
//=========================================================
class CEgonAmmo : public CBasePlayerAmmo
{
public:
    void Spawn() override
    {
        Precache();
        SET_MODEL(ENT(pev), "models/w_chainammo.mdl");
        CBasePlayerAmmo::Spawn();
    }

    void Precache() override
    {
        PRECACHE_MODEL("models/w_chainammo.mdl");
        PRECACHE_SOUND("items/9mmclip1.wav");
    }

    BOOL AddAmmo(CBaseEntity* pOther) override
    {
        if (pOther->GiveAmmo(AMMO_URANIUMBOX_GIVE, "uranium", URANIUM_MAX_CARRY) != -1)
        {
            EMIT_SOUND(ENT(pev), CHAN_ITEM, "items/9mmclip1.wav", 1.0f, ATTN_NORM);
            return TRUE;
        }
        return FALSE;
    }
};

LINK_ENTITY_TO_CLASS(ammo_egonclip, CEgonAmmo);

#endif