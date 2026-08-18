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

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "gamerules.h"

// special deathmatch shotgun spreads
const Vector VECTOR_CONE_DM_SHOTGUN = Vector(0.08716f, 0.04362f, 0.00f);      // 10 degrees by 5 degrees
const Vector VECTOR_CONE_DM_DOUBLESHOTGUN = Vector(0.17365f, 0.04362f, 0.00f); // 20 degrees by 5 degrees

// Shotgun animation sequence IDs
enum ShotgunAnim : int
{
    SHOTGUN_IDLE = 0,
    SHOTGUN_FIRE,
    SHOTGUN_FIRE2,
    SHOTGUN_RELOAD,
    SHOTGUN_PUMP,
    SHOTGUN_START_RELOAD,
    SHOTGUN_DRAW,
    SHOTGUN_HOLSTER,
    SHOTGUN_IDLE4,
    SHOTGUN_IDLE_DEEP
};

// Shotgun constants
constexpr float SHOTGUN_PUMP_TIME = 0.5f;
constexpr float SHOTGUN_FIRE_DELAY_PUMP = 0.75f;
constexpr float SHOTGUN_FIRE_DELAY_SEMI = 0.2f;
constexpr float SHOTGUN_IDLE_DELAY = 5.0f;
constexpr float SHOTGUN_EMPTY_IDLE_DELAY = 0.75f;
constexpr float SHOTGUN_RELOAD_START_DELAY = 0.6f;
constexpr float SHOTGUN_RELOAD_SHELL_DELAY = 0.5f;
constexpr float SHOTGUN_RELOAD_COOLDOWN = 1.0f;
constexpr float SHOTGUN_PUMP_COCK_DELAY = 1.5f;
constexpr int SHOTGUN_PELLETS_MP = 4;
constexpr int SHOTGUN_PELLETS_SP = 6;
constexpr float SHOTGUN_RANGE = 2048.0f;
constexpr float SHOTGUN_SWITCH_DELAY = 0.2f;

LINK_ENTITY_TO_CLASS(weapon_shotgun, CShotgun);

void CShotgun::Spawn()
{
    Precache();
    m_iId = WEAPON_SHOTGUN;
    SET_MODEL(ENT(pev), "models/w_shotgun.mdl");

    m_iDefaultAmmo = SHOTGUN_DEFAULT_GIVE;
    m_fireMode = FIRE_PUMP;

    FallInit(); // get ready to fall
}

void CShotgun::Precache()
{
    PRECACHE_MODEL("models/v_shotgun.mdl");
    PRECACHE_MODEL("models/w_shotgun.mdl");
    PRECACHE_MODEL("models/p_shotgun.mdl");

    m_iShell = PRECACHE_MODEL("models/shotgunshell.mdl"); // shotgun shell

    PRECACHE_SOUND("items/9mmclip1.wav");

    PRECACHE_SOUND("weapons/dbarrel1.wav"); // shotgun
    PRECACHE_SOUND("weapons/sbarrel1.wav"); // shotgun

    PRECACHE_SOUND("weapons/reload1.wav");  // shotgun reload
    PRECACHE_SOUND("weapons/reload3.wav");  // shotgun reload

    // PRECACHE_SOUND ("weapons/sshell1.wav"); // shotgun reload - played on client
    // PRECACHE_SOUND ("weapons/sshell3.wav"); // shotgun reload - played on client

    PRECACHE_SOUND("weapons/357_cock1.wav"); // gun empty sound
    PRECACHE_SOUND("weapons/scock1.wav");    // cock gun

    m_usSingleFire = PRECACHE_EVENT(1, "events/shotgun1.sc");
}

int CShotgun::AddToPlayer(CBasePlayer* pPlayer)
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

int CShotgun::GetItemInfo(ItemInfo* p)
{
    p->pszName = STRING(pev->classname);
    p->pszAmmo1 = "buckshot";
    p->iMaxAmmo1 = BUCKSHOT_MAX_CARRY;
    p->pszAmmo2 = nullptr;
    p->iMaxAmmo2 = -1;
    p->iMaxClip = SHOTGUN_MAX_CLIP;
    p->iSlot = 2;
    p->iPosition = 1;
    p->iFlags = 0;
    p->iId = m_iId = WEAPON_SHOTGUN;
    p->iWeight = SHOTGUN_WEIGHT;

    return 1;
}

BOOL CShotgun::Deploy()
{
    return DefaultDeploy("models/v_shotgun.mdl", "models/p_shotgun.mdl", SHOTGUN_DRAW, "shotgun");
}

void CShotgun::PrimaryAttack()
{
    // Don't fire underwater
    if (m_pPlayer->pev->waterlevel == 3)
    {
        PlayEmptySound();
        m_flNextPrimaryAttack = GetNextAttackDelay(0.15f);
        return;
    }

    // Check if we have ammo in the clip
    if (m_iClip <= 0)
    {
        Reload();
        if (m_iClip == 0)
        {
            PlayEmptySound();
        }
        return;
    }

    // Set weapon effects
    m_pPlayer->m_iWeaponVolume = LOUD_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;

    // Consume one round from the clip
    --m_iClip;

    // Determine event flags based on build configuration
#if defined(CLIENT_WEAPONS)
    const int eventFlags = FEV_NOTHOST;
#else
    const int eventFlags = 0;
#endif

    // Enable muzzle flash effect
    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;

    // Calculate shot origin and aiming direction
    const Vector vecSrc = m_pPlayer->GetGunPosition();
    const Vector vecAiming = m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);

    Vector vecDir;

    // Determine shot spread pattern based on game mode
#ifdef CLIENT_DLL
    const bool isMultiplayer = bIsMultiplayer();
#else
    const bool isMultiplayer = g_pGameRules->IsMultiplayer();
#endif

    if (isMultiplayer)
    {
        // Multiplayer: tighter spread, 4 pellets
        vecDir = m_pPlayer->FireBulletsPlayer(
            SHOTGUN_PELLETS_MP,
            vecSrc,
            vecAiming,
            VECTOR_CONE_DM_SHOTGUN,
            SHOTGUN_RANGE,
            BULLET_PLAYER_BUCKSHOT,
            0,
            0,
            m_pPlayer->pev,
            m_pPlayer->random_seed
        );
    }
    else
    {
        // Singleplayer: wider spread, 6 pellets
        vecDir = m_pPlayer->FireBulletsPlayer(
            SHOTGUN_PELLETS_SP,
            vecSrc,
            vecAiming,
            VECTOR_CONE_10DEGREES,
            SHOTGUN_RANGE,
            BULLET_PLAYER_BUCKSHOT,
            0,
            0,
            m_pPlayer->pev,
            m_pPlayer->random_seed
        );
    }

    // Play the firing event
    PLAYBACK_EVENT_FULL(
        eventFlags,
        m_pPlayer->edict(),
        m_usSingleFire,
        0.0f,
        (float*)&g_vecZero,
        (float*)&g_vecZero,
        vecDir.x,
        vecDir.y,
        0,
        0,
        0,
        0
    );

    // Check if we're completely out of ammo and notify the player
    if (m_iClip == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
    {
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
    }

    // Schedule pump action animation timing
    if (m_fireMode == FIRE_PUMP)
    {
        m_flPumpTime = gpGlobals->time + SHOTGUN_PUMP_TIME;
    }

    // Set attack cooldowns
    m_flNextPrimaryAttack = GetNextAttackDelay(
        (m_fireMode == FIRE_PUMP) ? SHOTGUN_FIRE_DELAY_PUMP : SHOTGUN_FIRE_DELAY_SEMI
    );

    // Set weapon idle timing
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
        ((m_iClip != 0) ? SHOTGUN_IDLE_DELAY : SHOTGUN_EMPTY_IDLE_DELAY);

    // Reset special reload state
    m_fInSpecialReload = 0;
}

void CShotgun::SecondaryAttack()
{
    // Toggle between pump and semi-auto modes
    m_fireMode = (m_fireMode == FIRE_PUMP) ? FIRE_SEMI_AUTO : FIRE_PUMP;

    // Play mode switch sound
    EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/357_cock1.wav", 0.8f, ATTN_NORM);

    // Set cooldown for mode switching
    m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + SHOTGUN_SWITCH_DELAY;
    m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + SHOTGUN_SWITCH_DELAY;
}

void CShotgun::Reload()
{
    // Check if we can reload
    if (m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0 || m_iClip == SHOTGUN_MAX_CLIP)
    {
        return;
    }

    // Don't reload until recoil is done
    if (m_flNextPrimaryAttack > UTIL_WeaponTimeBase())
    {
        return;
    }

    // Check reload state
    if (m_fInSpecialReload == 0)
    {
        // Start reload animation
        SendWeaponAnim(SHOTGUN_START_RELOAD);
        m_fInSpecialReload = 1;
        m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + SHOTGUN_RELOAD_START_DELAY;
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + SHOTGUN_RELOAD_START_DELAY;
        m_flNextPrimaryAttack = GetNextAttackDelay(SHOTGUN_RELOAD_COOLDOWN);
        m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + SHOTGUN_RELOAD_COOLDOWN;
        return;
    }

    if (m_fInSpecialReload == 1)
    {
        // Wait for gun to move to side
        if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
        {
            return;
        }

        m_fInSpecialReload = 2;

        // Play reload sound
        const char* reloadSound = (RANDOM_LONG(0, 1) == 0) ? "weapons/reload1.wav" : "weapons/reload3.wav";
        EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_ITEM, reloadSound, 1.0f, ATTN_NORM, 0, 85 + RANDOM_LONG(0, 0x1f));

        SendWeaponAnim(SHOTGUN_RELOAD);

        m_flNextReload = UTIL_WeaponTimeBase() + SHOTGUN_RELOAD_SHELL_DELAY;
        m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + SHOTGUN_RELOAD_SHELL_DELAY;
        return;
    }

    // Add shell to clip
    ++m_iClip;
    --m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType];
    m_fInSpecialReload = 1;
}

void CShotgun::WeaponIdle()
{
    ResetEmptySound();

    m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);

    if (m_flTimeWeaponIdle < UTIL_WeaponTimeBase())
    {
        if (m_iClip == 0 && m_fInSpecialReload == 0 && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
        {
            Reload();
            return;
        }

        if (m_fInSpecialReload != 0)
        {
            if (m_iClip < SHOTGUN_MAX_CLIP && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] > 0)
            {
                Reload();
            }
            else
            {
                // Reload debounce has timed out - finish with pump
                SendWeaponAnim(SHOTGUN_PUMP);

                // Play cocking sound
                EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_ITEM, "weapons/scock1.wav",
                    1.0f, ATTN_NORM, 0, 95 + RANDOM_LONG(0, 0x1f));
                m_fInSpecialReload = 0;
                m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + SHOTGUN_PUMP_COCK_DELAY;
            }
            return;
        }

        // Normal idle animation selection
        const float flRand = UTIL_SharedRandomFloat(m_pPlayer->random_seed, 0.0f, 1.0f);
        int iAnim;

        if (flRand <= 0.8f)
        {
            iAnim = SHOTGUN_IDLE_DEEP;
            m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + (60.0f / 12.0f);
        }
        else if (flRand <= 0.95f)
        {
            iAnim = SHOTGUN_IDLE;
            m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + (20.0f / 9.0f);
        }
        else
        {
            iAnim = SHOTGUN_IDLE4;
            m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + (20.0f / 9.0f);
        }

        SendWeaponAnim(iAnim);
    }
}

void CShotgun::ItemPostFrame()
{
    // Play pump sound after firing
    if (m_flPumpTime > 0.0f && m_flPumpTime < gpGlobals->time)
    {
        EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_ITEM, "weapons/scock1.wav",
            1.0f, ATTN_NORM, 0, 95 + RANDOM_LONG(0, 0x1f));
        m_flPumpTime = 0.0f;
    }

    CBasePlayerWeapon::ItemPostFrame();
}

//=========================================================
// Shotgun Ammo Entity
//=========================================================
class CShotgunAmmo : public CBasePlayerAmmo
{
public:
    void Spawn() override
    {
        Precache();
        SET_MODEL(ENT(pev), "models/w_shotbox.mdl");
        CBasePlayerAmmo::Spawn();
    }

    void Precache() override
    {
        PRECACHE_MODEL("models/w_shotbox.mdl");
        PRECACHE_SOUND("items/9mmclip1.wav");
    }

    BOOL AddAmmo(CBaseEntity* pOther) override
    {
        if (pOther->GiveAmmo(AMMO_BUCKSHOTBOX_GIVE, "buckshot", BUCKSHOT_MAX_CARRY) != -1)
        {
            EMIT_SOUND(ENT(pev), CHAN_ITEM, "items/9mmclip1.wav", 1.0f, ATTN_NORM);
            return TRUE;
        }
        return FALSE;
    }
};

LINK_ENTITY_TO_CLASS(ammo_buckshot, CShotgunAmmo);