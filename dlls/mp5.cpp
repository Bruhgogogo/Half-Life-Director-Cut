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
#include "soundent.h"
#include "gamerules.h"

// Animation sequence IDs
enum MP5Anim : int
{
    MP5_LONGIDLE = 0,
    MP5_IDLE1,
    MP5_LAUNCH,
    MP5_RELOAD,
    MP5_DEPLOY,
    MP5_FIRE1,
    MP5_FIRE2,
    MP5_FIRE3,
};

// Constants
constexpr float MP5_FIRE_DELAY = 0.086f;
constexpr float MP5_EMPTY_DELAY = 0.15f;
constexpr float MP5_GRENADE_ATTACK_DELAY = 1.0f;
constexpr float MP5_GRENADE_IDLE_DELAY = 5.0f;
constexpr float MP5_IDLE_MIN = 10.0f;
constexpr float MP5_IDLE_MAX = 15.0f;
constexpr float MP5_GRENADE_SPEED = 800.0f;
constexpr float MP5_GRENADE_OFFSET = 16.0f;
constexpr float MP5_RELOAD_TIME = 1.5f;
constexpr float MP5_RANGE = 8192.0f;
constexpr int MP5_GRENADE_DAMAGE = 2;
constexpr int MP5_BULLET_DAMAGE = 2;

// Model and sound paths
constexpr char* MP5_V_MODEL = "models/v_9mmAR.mdl";
constexpr char* MP5_W_MODEL = "models/w_9mmAR.mdl";
constexpr char* MP5_P_MODEL = "models/p_9mmAR.mdl";
constexpr char* MP5_SHELL_MODEL = "models/shell.mdl";
constexpr char* MP5_GRENADE_MODEL = "models/grenade.mdl";
constexpr char* MP5_CLIP_MODEL = "models/w_9mmARclip.mdl";
constexpr char* MP5_CLIP_SOUND = "items/9mmclip1.wav";
constexpr char* MP5_INSERT_SOUND = "items/clipinsert1.wav";
constexpr char* MP5_RELEASE_SOUND = "items/cliprelease1.wav";
constexpr char* MP5_FIRE_SOUND1 = "weapons/hks1.wav";
constexpr char* MP5_FIRE_SOUND2 = "weapons/hks2.wav";
constexpr char* MP5_FIRE_SOUND3 = "weapons/hks3.wav";
constexpr char* MP5_GL_LAUNCH = "weapons/glauncher.wav";
constexpr char* MP5_GL_LAUNCH2 = "weapons/glauncher2.wav";
constexpr char* MP5_COCK_SOUND = "weapons/357_cock1.wav";
constexpr char* MP5_EVENT_FIRE = "events/mp5.sc";
constexpr char* MP5_EVENT_GRENADE = "events/mp52.sc";

LINK_ENTITY_TO_CLASS(weapon_mp5, CMP5);
LINK_ENTITY_TO_CLASS(weapon_9mmAR, CMP5);

int CMP5::SecondaryAmmoIndex()
{
    return m_iSecondaryAmmoType;
}

void CMP5::Spawn()
{
    pev->classname = MAKE_STRING("weapon_9mmAR"); // hack to allow for old names
    Precache();
    SET_MODEL(ENT(pev), MP5_W_MODEL);
    m_iId = WEAPON_MP5;

    m_iDefaultAmmo = gpGlobals->maxClients > 1 ? MP5_MAX_CLIP : MP5_DEFAULT_GIVE;

    FallInit(); // get ready to fall down.
}

void CMP5::Precache()
{
    PRECACHE_MODEL(MP5_V_MODEL);
    PRECACHE_MODEL(MP5_W_MODEL);
    PRECACHE_MODEL(MP5_P_MODEL);

    m_iShell = PRECACHE_MODEL(MP5_SHELL_MODEL); // brass shell

    PRECACHE_MODEL(MP5_GRENADE_MODEL); // grenade
    PRECACHE_MODEL(MP5_CLIP_MODEL);

    PRECACHE_SOUND(MP5_CLIP_SOUND);
    PRECACHE_SOUND(MP5_INSERT_SOUND);
    PRECACHE_SOUND(MP5_RELEASE_SOUND);

    PRECACHE_SOUND(MP5_FIRE_SOUND1);
    PRECACHE_SOUND(MP5_FIRE_SOUND2);
    PRECACHE_SOUND(MP5_FIRE_SOUND3);

    PRECACHE_SOUND(MP5_GL_LAUNCH);
    PRECACHE_SOUND(MP5_GL_LAUNCH2);
    PRECACHE_SOUND(MP5_COCK_SOUND);

    m_usMP5 = PRECACHE_EVENT(1, MP5_EVENT_FIRE);
    m_usMP52 = PRECACHE_EVENT(1, MP5_EVENT_GRENADE);
}

int CMP5::GetItemInfo(ItemInfo* p)
{
    p->pszName = STRING(pev->classname);
    p->pszAmmo1 = "9mm";
    p->iMaxAmmo1 = _9MM_MAX_CARRY;
    p->pszAmmo2 = "ARgrenades";
    p->iMaxAmmo2 = M203_GRENADE_MAX_CARRY;
    p->iMaxClip = MP5_MAX_CLIP;
    p->iSlot = 2;
    p->iPosition = 0;
    p->iFlags = 0;
    p->iId = m_iId = WEAPON_MP5;
    p->iWeight = MP5_WEIGHT;

    return 1;
}

int CMP5::AddToPlayer(CBasePlayer* pPlayer)
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

BOOL CMP5::Deploy()
{
    return DefaultDeploy(MP5_V_MODEL, MP5_P_MODEL, MP5_DEPLOY, "mp5");
}

void CMP5::PrimaryAttack()
{
    // Don't fire underwater
    if (m_pPlayer->pev->waterlevel == 3)
    {
        PlayEmptySound();
        m_flNextPrimaryAttack = MP5_EMPTY_DELAY;
        return;
    }

    if (m_iClip <= 0)
    {
        PlayEmptySound();
        m_flNextPrimaryAttack = MP5_EMPTY_DELAY;
        return;
    }

    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = NORMAL_GUN_FLASH;

    --m_iClip;

    m_pPlayer->pev->effects |= EF_MUZZLEFLASH;

    // Player "shoot" animation
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);

    const Vector vecSrc = m_pPlayer->GetGunPosition();
    const Vector vecAiming = m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);
    Vector vecDir;

#ifdef CLIENT_DLL
    const bool isMultiplayer = bIsMultiplayer();
#else
    const bool isMultiplayer = g_pGameRules->IsMultiplayer();
#endif

    if (isMultiplayer)
    {
        // Optimized multiplayer - widened to make it easier to hit moving players
        vecDir = m_pPlayer->FireBulletsPlayer(
            1, vecSrc, vecAiming,
            VECTOR_CONE_6DEGREES,
            MP5_RANGE,
            BULLET_PLAYER_MP5,
            MP5_BULLET_DAMAGE,
            0,
            m_pPlayer->pev,
            m_pPlayer->random_seed
        );
    }
    else
    {
        // Single player spread
        vecDir = m_pPlayer->FireBulletsPlayer(
            1, vecSrc, vecAiming,
            VECTOR_CONE_3DEGREES,
            MP5_RANGE,
            BULLET_PLAYER_MP5,
            MP5_BULLET_DAMAGE,
            0,
            m_pPlayer->pev,
            m_pPlayer->random_seed
        );
    }

#if defined(CLIENT_WEAPONS)
    const int eventFlags = FEV_NOTHOST;
#else
    const int eventFlags = 0;
#endif

    PLAYBACK_EVENT_FULL(
        eventFlags,
        m_pPlayer->edict(),
        m_usMP5,
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

    if (!m_iClip && m_pPlayer->m_rgAmmo[m_iPrimaryAmmoType] <= 0)
    {
        // HEV suit - indicate out of ammo condition
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
    }

    m_flNextPrimaryAttack = GetNextAttackDelay(MP5_FIRE_DELAY);

    if (m_flNextPrimaryAttack < UTIL_WeaponTimeBase())
    {
        m_flNextPrimaryAttack = UTIL_WeaponTimeBase() + MP5_FIRE_DELAY;
    }

    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() +
        UTIL_SharedRandomFloat(m_pPlayer->random_seed, MP5_IDLE_MIN, MP5_IDLE_MAX);
}

void CMP5::SecondaryAttack()
{
    // Don't fire underwater
    if (m_pPlayer->pev->waterlevel == 3)
    {
        PlayEmptySound();
        m_flNextPrimaryAttack = MP5_EMPTY_DELAY;
        return;
    }

    if (m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType] == 0)
    {
        PlayEmptySound();
        return;
    }

    m_pPlayer->m_iWeaponVolume = NORMAL_GUN_VOLUME;
    m_pPlayer->m_iWeaponFlash = BRIGHT_GUN_FLASH;

    m_pPlayer->m_iExtraSoundTypes = bits_SOUND_DANGER;
    m_pPlayer->m_flStopExtraSoundTime = UTIL_WeaponTimeBase() + 0.2f;

    --m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType];

    // Player "shoot" animation
    m_pPlayer->SetAnimation(PLAYER_ATTACK1);

    UTIL_MakeVectors(m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle);

    // We don't add in player velocity anymore
    CGrenade::ShootContact(
        m_pPlayer->pev,
        m_pPlayer->pev->origin + m_pPlayer->pev->view_ofs + gpGlobals->v_forward * MP5_GRENADE_OFFSET,
        gpGlobals->v_forward * MP5_GRENADE_SPEED
    );

#if defined(CLIENT_WEAPONS)
    const int eventFlags = FEV_NOTHOST;
#else
    const int eventFlags = 0;
#endif

    PLAYBACK_EVENT(eventFlags, m_pPlayer->edict(), m_usMP52);

    m_flNextPrimaryAttack = GetNextAttackDelay(MP5_GRENADE_ATTACK_DELAY);
    m_flNextSecondaryAttack = UTIL_WeaponTimeBase() + MP5_GRENADE_ATTACK_DELAY;
    m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + MP5_GRENADE_IDLE_DELAY;

    if (!m_pPlayer->m_rgAmmo[m_iSecondaryAmmoType])
    {
        // HEV suit - indicate out of ammo condition
        m_pPlayer->SetSuitUpdate("!HEV_AMO0", FALSE, 0);
    }
}

void CMP5::Reload()
{
    if (m_pPlayer->ammo_9mm <= 0)
        return;

    DefaultReload(MP5_MAX_CLIP, MP5_RELOAD, MP5_RELOAD_TIME);
}

void CMP5::WeaponIdle()
{
    ResetEmptySound();

    m_pPlayer->GetAutoaimVector(AUTOAIM_5DEGREES);

    if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
        return;

    const int iAnim = (RANDOM_LONG(0, 1) == 0) ? MP5_LONGIDLE : MP5_IDLE1;

    SendWeaponAnim(iAnim);

    m_flTimeWeaponIdle = UTIL_SharedRandomFloat(
        m_pPlayer->random_seed,
        MP5_IDLE_MIN,
        MP5_IDLE_MAX
    );
}

//=========================================================
// MP5 Ammo Clip
//=========================================================
class CMP5AmmoClip : public CBasePlayerAmmo
{
public:
    void Spawn() override
    {
        Precache();
        SET_MODEL(ENT(pev), MP5_CLIP_MODEL);
        CBasePlayerAmmo::Spawn();
    }

    void Precache() override
    {
        PRECACHE_MODEL(MP5_CLIP_MODEL);
        PRECACHE_SOUND(MP5_CLIP_SOUND);
    }

    BOOL AddAmmo(CBaseEntity* pOther) override
    {
        const int bResult = (pOther->GiveAmmo(AMMO_MP5CLIP_GIVE, "9mm", _9MM_MAX_CARRY) != -1);
        if (bResult)
        {
            EMIT_SOUND(ENT(pev), CHAN_ITEM, MP5_CLIP_SOUND, 1.0f, ATTN_NORM);
        }
        return bResult;
    }
};

LINK_ENTITY_TO_CLASS(ammo_mp5clip, CMP5AmmoClip);
LINK_ENTITY_TO_CLASS(ammo_9mmAR, CMP5AmmoClip);

//=========================================================
// MP5 Chain Ammo Box
//=========================================================
class CMP5Chainammo : public CBasePlayerAmmo
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
        PRECACHE_SOUND(MP5_CLIP_SOUND);
    }

    BOOL AddAmmo(CBaseEntity* pOther) override
    {
        const int bResult = (pOther->GiveAmmo(AMMO_CHAINBOX_GIVE, "9mm", _9MM_MAX_CARRY) != -1);
        if (bResult)
        {
            EMIT_SOUND(ENT(pev), CHAN_ITEM, MP5_CLIP_SOUND, 1.0f, ATTN_NORM);
        }
        return bResult;
    }
};

LINK_ENTITY_TO_CLASS(ammo_9mmbox, CMP5Chainammo);

//=========================================================
// MP5 Grenade Ammo
//=========================================================
class CMP5AmmoGrenade : public CBasePlayerAmmo
{
public:
    void Spawn() override
    {
        Precache();
        SET_MODEL(ENT(pev), "models/w_ARgrenade.mdl");
        CBasePlayerAmmo::Spawn();
    }

    void Precache() override
    {
        PRECACHE_MODEL("models/w_ARgrenade.mdl");
        PRECACHE_SOUND(MP5_CLIP_SOUND);
    }

    BOOL AddAmmo(CBaseEntity* pOther) override
    {
        const int bResult = (pOther->GiveAmmo(AMMO_M203BOX_GIVE, "ARgrenades", M203_GRENADE_MAX_CARRY) != -1);
        if (bResult)
        {
            EMIT_SOUND(ENT(pev), CHAN_ITEM, MP5_CLIP_SOUND, 1.0f, ATTN_NORM);
        }
        return bResult;
    }
};

LINK_ENTITY_TO_CLASS(ammo_mp5grenades, CMP5AmmoGrenade);
LINK_ENTITY_TO_CLASS(ammo_ARgrenades, CMP5AmmoGrenade);