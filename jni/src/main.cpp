#include "main.h"
#include <linux/input.h>
#include <linux/uinput.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <malloc.h>
#include <pthread.h>
#include <sstream>
#include <string>
#include <sys/system_properties.h>
#include <unistd.h>
#include <vector>
#include "Memory/Memory.h"
#include "Memory/PatternScanner.h"
#include "Quaternion.hpp"
#include "Vector2.hpp"
#include "Vector3.hpp"
#include "Includes/Log.h"
#include "Includes/Offset.h"
#include "Engine/CanvasView.h"
#include "include.h"
#include "Matrix4x4.hpp"
#include "ToString.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "icon/HeroIcons.h"
#include "Decoder64.h"
#include "DrawIconHero.h"

using namespace Memory;

//=====================================================================================
// GLOBAL
//=====================================================================================
bool main_thread_flag = true;

int abs_ScreenX = 0;
int abs_ScreenY = 0;

// ESP toggles
bool drawMLine            = true;
bool iconhero             = true;
bool drawHealthBar        = true;
bool drawDistance         = true;
bool drawHeroName         = true;
bool drawMonsterName      = true;
bool drawAlertUnderAttack = true;
bool drawMinionDot        = true;

// ESP size settings
float heroEspSize    = 22.5f;
float monsterEspSize = 20.0f;
float bossEspSize    = 24.0f;

// Icon hero diperkecil, tanpa settingan
static const float HeroIconRadius = 30.0f;
static const float MinionDotRadius = 3.5f;

float RadiusCir = 50.0f;

uintptr_t libbase = 0;

//=====================================================================================
// OFFSET TERBARU
//=====================================================================================
namespace Offsets
{
    constexpr uintptr_t BattleManagerClassSlot = 0x635B290;
    constexpr uintptr_t GameMethodClassSlot    = 0x635B620;

    constexpr size_t StaticFieldsOffset = 0xA8;
    constexpr size_t ClassInstanceOff   = 0x0;

    namespace BattleManager
    {
        constexpr size_t Instance         = 0x0;
        constexpr size_t LocalPlayerShow  = 0x48;
        constexpr size_t ShowPlayers      = 0x70;
        constexpr size_t ShowMonsters     = 0x78;
    }

    namespace ShowEntity
    {
        constexpr size_t HeroID          = 0x18C;
        constexpr size_t Type            = 0x78;
        constexpr size_t Death           = 0xC5;
        constexpr size_t Hp              = 0x1A4;
        constexpr size_t HpMax           = 0x1A8;
        constexpr size_t Level           = 0x190;
        constexpr size_t CachePosition   = 0x28C;
        constexpr size_t SameCampType    = 0x2A9;
        constexpr size_t EntityCampType  = 0xD0;
        constexpr size_t RoleName        = 0x460;
    }

    namespace ShowPlayer
    {
        constexpr size_t HeroName       = 0x8F0;
        constexpr size_t KillWildTimes  = 0xA28;
    }

    namespace Camera
    {
        constexpr size_t StaticSmoothFollow       = 0x10;
        constexpr size_t SmoothFollowManagedCam   = 0x48;
        constexpr size_t ManagedCameraNativeCam   = 0x10;
        constexpr size_t ViewMatrixOffset         = 0x5C;
    }

    namespace List
    {
        constexpr size_t Items      = 0x10;
        constexpr size_t Size       = 0x18;
        constexpr size_t DataOffset = 0x20;
        constexpr size_t Stride     = 0x8;
    }

    namespace Il2cppString
    {
        constexpr size_t Length = 0x10;
        constexpr size_t Buffer = 0x14;
    }
}

//=====================================================================================
// CAMERA STRUCT
//=====================================================================================
struct Camera
{
    Matrix4x4 worldToCameraMatrix;
    Matrix4x4 projectionMatrix;
};

Matrix4x4 _vMatrix;

//=====================================================================================
// MONSTER / RETRIBUTION DATA
//=====================================================================================
int ListMonsterId[] =
{
    2002, 2003, 2004, 2005, 2006, 2008, 2009, 2011,
    2012, 2013, 2056, 2059, 2072,
    2220, 2221, 2222, 2223, 2224, 2225, 2226, 2227,
    2228, 2229, 2230, 2232,
};

struct MonsterData
{
    uintptr_t address;
    int id;
    Vector3 position;
    float distance;
    int health;
    int maxHP;
    bool isDead;
    bool isVisible;
    bool isValid;
    char name[100];
};

MonsterData monster[20];
int MonsterCount = 0;

uintptr_t Oneself = 0;

bool lastRetriTriggered[20] = { false };

bool autoRetribution        = false;
bool AutoRetributionRed     = false;
bool AutoRetributionBlue    = false;
bool AutoRetributionLord    = false;
bool AutoRetributionTurtle  = false;
bool AutoRetributionCrab    = false;
bool AutoRetributionLito    = false;

float retriTouchX = 1575.0f;
float retriTouchY = 661.0f;

//=====================================================================================
// MINIMAP
//=====================================================================================
int MinimapSize = 342;
int MinimapPos  = 76;

bool MinimapIcon = true;
bool HideLine    = false;

float g_MinimapScale   = 74.11f;
float g_Res0_MultX     = 1.0f;
float g_Res0_MultY     = 1.0f;
float g_Res1_OffsetX   = 0.0f;
float g_Res1_OffsetY   = 0.0f;
int g_ICSize           = 38;

//=====================================================================================
// BASIC HELPERS
//=====================================================================================
static uintptr_t ReadPtrEx(uintptr_t address)
{
    if (!address)
        return 0;

    return static_cast<uintptr_t>(getPtr641(address));
}

static float Clamp01(float value)
{
    if (value < 0.0f)
        return 0.0f;

    if (value > 1.0f)
        return 1.0f;

    return value;
}

std::string fshy(uintptr_t address)
{
    if (!address)
        return "";

    uint32_t stringLength = Read<uint32_t>(address + Offsets::Il2cppString::Length);
    if (stringLength == 0 || stringLength > 1024)
        return "";

    std::vector<char16_t> buffer(stringLength + 1, 0);

    pvm(
        reinterpret_cast<void*>(address + Offsets::Il2cppString::Buffer),
        reinterpret_cast<void*>(buffer.data()),
        static_cast<size_t>(stringLength) * sizeof(char16_t),
        false
    );

    return utf16_to_utf8(buffer.data(), stringLength);
}

static uintptr_t GetBattleManager()
{
    uintptr_t klass = ReadPtrEx(libbase + Offsets::BattleManagerClassSlot);
    if (!klass)
        return 0;

    uintptr_t staticFields = ReadPtrEx(klass + Offsets::StaticFieldsOffset);
    if (!staticFields)
        return 0;

    return ReadPtrEx(staticFields + Offsets::BattleManager::Instance);
}

static uintptr_t GetNativeCamera()
{
    uintptr_t klass = ReadPtrEx(libbase + Offsets::GameMethodClassSlot);
    if (!klass)
        return 0;

    uintptr_t staticFields = ReadPtrEx(klass + Offsets::StaticFieldsOffset);
    if (!staticFields)
        return 0;

    uintptr_t smoothFollow = ReadPtrEx(staticFields + Offsets::Camera::StaticSmoothFollow);
    if (!smoothFollow)
        return 0;

    uintptr_t managedCamera = ReadPtrEx(smoothFollow + Offsets::Camera::SmoothFollowManagedCam);
    if (!managedCamera)
        return 0;

    return ReadPtrEx(managedCamera + Offsets::Camera::ManagedCameraNativeCam);
}

static uintptr_t GetListData(uintptr_t listObject)
{
    if (!listObject)
        return 0;

    uintptr_t items = ReadPtrEx(listObject + Offsets::List::Items);
    if (!items)
        return 0;

    return items + Offsets::List::DataOffset;
}

static int GetListCount(uintptr_t listObject, int maxCount)
{
    if (!listObject)
        return 0;

    int count = Read<int>(listObject + Offsets::List::Size);
    if (count < 0)
        return 0;

    if (count > maxCount)
        count = maxCount;

    return count;
}

static ImVec2 CalcScaledTextSize(const char* text, float fontSize)
{
    ImVec2 size = ImGui::CalcTextSize(text, nullptr, false, 0.0f);

    float baseFontSize = ImGui::GetFontSize();
    if (baseFontSize > 0.0f && fontSize > 0.0f)
    {
        float scale = fontSize / baseFontSize;
        size.x *= scale;
        size.y = fontSize;
    }

    return size;
}

static void DrawEspText(
    ImDrawList* draw,
    float fontSize,
    float x,
    float y,
    ImColor color,
    const char* text
)
{
    if (!draw)
        return;

    if (!text)
        return;

    ImVec2 pos(x, y);

    draw->AddText(
        nullptr,
        fontSize,
        ImVec2(pos.x + 1.0f, pos.y + 1.0f),
        IM_COL32(0, 0, 0, 180),
        text
    );

    draw->AddText(
        nullptr,
        fontSize,
        pos,
        color,
        text
    );
}

//=====================================================================================
// HEALTH BAR
//=====================================================================================
static void DrawHealthBarHorizontal(
    ImDrawList* draw,
    ImVec2 pos,
    float width,
    float height,
    float currentHP,
    float maxHP
)
{
    if (!draw)
        return;

    if (width <= 0.0f || height <= 0.0f)
        return;

    float percent = 0.0f;
    if (maxHP > 0.0f)
        percent = currentHP / maxHP;

    percent = Clamp01(percent);

    float rounding = std::min(height / 2.0f, 4.0f);
    ImVec2 pMax(pos.x + width, pos.y + height);

    // Background
    draw->AddRectFilled(pos, pMax, IM_COL32(0, 0, 0, 180), rounding);

    // Fill
    if (percent > 0.0f)
    {
        ImU32 fillColor;

        if (percent > 0.6f)
            fillColor = IM_COL32(60, 220, 90, 235);
        else if (percent > 0.3f)
            fillColor = IM_COL32(255, 200, 0, 235);
        else
            fillColor = IM_COL32(255, 60, 60, 235);

        ImVec2 fillMax(pos.x + (width * percent), pos.y + height);
        draw->AddRectFilled(pos, fillMax, fillColor, rounding);
    }

    // Outline
    draw->AddRect(pos, pMax, IM_COL32(255, 255, 255, 210), rounding, 0, 1.0f);
}

static void DrawHealthBarVertical(
    ImDrawList* draw,
    ImVec2 pos,
    float width,
    float height,
    float currentHP,
    float maxHP
)
{
    if (!draw)
        return;

    if (width <= 0.0f || height <= 0.0f)
        return;

    float percent = 0.0f;
    if (maxHP > 0.0f)
        percent = currentHP / maxHP;

    percent = Clamp01(percent);

    float rounding = std::min(width / 2.0f, 4.0f);
    ImVec2 pMax(pos.x + width, pos.y + height);

    // Background
    draw->AddRectFilled(pos, pMax, IM_COL32(0, 0, 0, 180), rounding);

    // Fill dari bawah ke atas
    if (percent > 0.0f)
    {
        ImU32 fillColor;

        if (percent > 0.6f)
            fillColor = IM_COL32(60, 220, 90, 235);
        else if (percent > 0.3f)
            fillColor = IM_COL32(255, 200, 0, 235);
        else
            fillColor = IM_COL32(255, 60, 60, 235);

        float fillHeight = height * percent;
        float fillRounding = std::min(rounding, fillHeight / 2.0f);

        ImVec2 fillMin(pos.x, pos.y + height - fillHeight);
        ImVec2 fillMax(pos.x + width, pos.y + height);

        draw->AddRectFilled(fillMin, fillMax, fillColor, fillRounding);
    }

    // Outline
    draw->AddRect(pos, pMax, IM_COL32(255, 255, 255, 210), rounding, 0, 1.0f);
}

//=====================================================================================
// WORLD TO SCREEN / MINIMAP
//=====================================================================================
bool WorldToScreen(Vector3 from, Vector2* to)
{
    if (!to)
        return false;

    auto viewMatrix = _vMatrix.MultiplyPoint(from);
    auto screenPos = Vector3(viewMatrix.X + 1.0f, viewMatrix.Y + 1.0f, viewMatrix.Z + 1.0f) / 2.0f;

    *to = Vector2(
        screenPos.X * abs_ScreenX,
        abs_ScreenY - (screenPos.Y * abs_ScreenY)
    );

    return viewMatrix != Vector3::Zero();
}

void FindPoint(Vector2 origin, Vector2& point, int screenwidth, int screenheight, float length)
{
    float halfScreenWidth  = screenwidth / 2.0f;
    float halfScreenHeight = screenheight / 2.0f;

    float halfScreenWidth2  = std::max(0.0f, (screenwidth - length) / 2.0f);
    float halfScreenHeight2 = std::max(0.0f, (screenheight - length) / 2.0f);

    float dx = fabs(origin.X - halfScreenWidth);
    float dy = fabs(origin.Y - halfScreenHeight);

    float rx = (dx != 0.0f) ? halfScreenWidth2 / dx : 0.0f;
    float ry = (dy != 0.0f) ? halfScreenHeight2 / dy : 0.0f;
    float r  = fmin(rx, ry);

    point.X = origin.X + (halfScreenWidth - origin.X) * (1.0f - r);
    point.Y = origin.Y + (halfScreenHeight - origin.Y) * (1.0f - r);
}

Vector2 WorldToMinimap(Vector3 HeroPosition)
{
    const float deg2rad = 0.017453292519943295f;

    float angle = 314.60f * deg2rad;
    float angleCos = std::cos(angle);
    float angleSin = std::sin(angle);

    Vector2 Res0;
    Res0.X = ((angleCos * HeroPosition.X - angleSin * (-HeroPosition.Z)) / g_MinimapScale) * g_Res0_MultX;
    Res0.Y = ((angleSin * HeroPosition.X + angleCos * (-HeroPosition.Z)) / g_MinimapScale) * g_Res0_MultY;

    Vector2 Res1;
    Res1.X = (Res0.X * MinimapSize) + MinimapPos + MinimapSize / 2.0f + g_Res1_OffsetX;
    Res1.Y = (Res0.Y * MinimapSize) + MinimapSize / 2.0f + g_Res1_OffsetY;

    return Res1;
}

//=====================================================================================
// MONSTER FILTER
//=====================================================================================
bool bMonster(int iValue)
{
    return std::find(std::begin(ListMonsterId), std::end(ListMonsterId), iValue) != std::end(ListMonsterId);
}

//=====================================================================================
// TOUCH
//=====================================================================================
void Touch_Tap(int x, int y)
{
    Touch_Down((float)x, (float)y);
    usleep(80000);
    Touch_Up();
}

//=====================================================================================
// RETRIBUTION LOGIC
//=====================================================================================
int CalculateRetriDamage(int Level, int KillWild)
{
    if (Level < 1)
        Level = 1;

    if (KillWild < 0)
        KillWild = 0;

    if (KillWild < 5)
        return 1000 + (Level - 1) * 80;

    return (1000 + (Level - 1) * 80) + (300 + (Level - 1) * 40);
}

void MonsterRetribution()
{
    MonsterCount = 0;

    uintptr_t BattleManager = GetBattleManager();
    if (!BattleManager)
    {
        Oneself = 0;
        return;
    }

    Oneself = ReadPtrEx(BattleManager + Offsets::BattleManager::LocalPlayerShow);
    if (!Oneself)
        return;

    Vector3 MyPosition{};
    vm_readv(Oneself + Offsets::ShowEntity::CachePosition, &MyPosition, sizeof(MyPosition));

    uintptr_t Showmonster = ReadPtrEx(BattleManager + Offsets::BattleManager::ShowMonsters);
    if (!Showmonster)
        return;

    uintptr_t monsterDataArray = GetListData(Showmonster);
    int monsterCount = GetListCount(Showmonster, 128);

    if (!monsterDataArray || monsterCount <= 0)
        return;

    int monsterfound = 0;

    for (int i = 0; i < monsterCount && monsterfound < 20; i++)
    {
        uintptr_t currentMonsterPtr = ReadPtrEx(monsterDataArray + (i * Offsets::List::Stride));
        if (!currentMonsterPtr)
            continue;

        int monsterID    = Read<int>(currentMonsterPtr + Offsets::ShowEntity::HeroID);
        int monsterHP    = static_cast<int>(Read<uint64_t>(currentMonsterPtr + Offsets::ShowEntity::Hp));
        int monsterMaxHP = static_cast<int>(Read<uint64_t>(currentMonsterPtr + Offsets::ShowEntity::HpMax));

        if (monsterHP <= 0 || monsterMaxHP <= 0)
            continue;

        Vector3 monsterPos{};
        vm_readv(currentMonsterPtr + Offsets::ShowEntity::CachePosition, &monsterPos, sizeof(monsterPos));

        uint8_t deadFlag = Read<uint8_t>(currentMonsterPtr + Offsets::ShowEntity::Death);
        bool mDead = (deadFlag != 0);

        if (mDead)
            continue;

        std::string mName = MonsterToString(monsterID);
        if (mName.empty())
        {
            switch (monsterID)
            {
                case 2002:
                    mName = "Lord";
                    break;

                case 2003:
                    mName = "Turtle";
                    break;

                case 2004:
                    mName = "Red Buff";
                    break;

                case 2005:
                    mName = "Blue Buff";
                    break;

                case 2006:
                    mName = "Crab";
                    break;

                case 2056:
                    mName = "Lito";
                    break;

                default:
                    continue;
            }
        }

        monster[monsterfound].address  = currentMonsterPtr;
        monster[monsterfound].id       = monsterID;
        monster[monsterfound].position = monsterPos;
        monster[monsterfound].distance = Vector3::Distance(MyPosition, monsterPos);
        monster[monsterfound].health   = monsterHP;
        monster[monsterfound].maxHP    = monsterMaxHP;
        monster[monsterfound].isDead   = mDead;
        monster[monsterfound].isVisible = true;
        monster[monsterfound].isValid  = true;

        strncpy(monster[monsterfound].name, mName.c_str(), sizeof(monster[monsterfound].name) - 1);
        monster[monsterfound].name[sizeof(monster[monsterfound].name) - 1] = '\0';

        monsterfound++;
    }

    MonsterCount = monsterfound;
}

void CheckAndTriggerRetribution()
{
    if (!autoRetribution || !Oneself || MonsterCount <= 0)
        return;

    int myLevel  = Read<int>(Oneself + Offsets::ShowEntity::Level);
    int killWild = Read<int>(Oneself + Offsets::ShowPlayer::KillWildTimes);

    if (myLevel < 1)
        myLevel = 1;

    if (killWild < 0)
        killWild = 0;

    int retriDmg = CalculateRetriDamage(myLevel, killWild);

    for (int i = 0; i < MonsterCount; i++)
    {
        if (!monster[i].isValid || monster[i].isDead)
        {
            lastRetriTriggered[i] = false;
            continue;
        }

        if (monster[i].distance > 5.0f)
        {
            lastRetriTriggered[i] = false;
            continue;
        }

        int id = monster[i].id;
        bool isTarget = false;

        if (AutoRetributionLord   && id == 2002) isTarget = true;
        if (AutoRetributionTurtle && id == 2003) isTarget = true;
        if (AutoRetributionBlue   && id == 2005) isTarget = true;
        if (AutoRetributionRed    && id == 2004) isTarget = true;
        if (AutoRetributionCrab   && id == 2006) isTarget = true;
        if (AutoRetributionLito   && id == 2056) isTarget = true;

        if (!isTarget)
        {
            lastRetriTriggered[i] = false;
            continue;
        }

        if (monster[i].health <= retriDmg)
        {
            if (!lastRetriTriggered[i])
            {
                Touch_Tap((int)retriTouchX, (int)retriTouchY);
                lastRetriTriggered[i] = true;
            }
        }
        else
        {
            lastRetriTriggered[i] = false;
        }
    }
}

//=====================================================================================
// MINIMAP HERO ICON
//=====================================================================================
static void DrawMinimapHeroIcon(ImDrawList* draw, ImVec2 center, int heroId, float size)
{
    if (!draw)
        return;

    if (size <= 0.0f)
        return;

    float half = size / 2.0f;

    ImVec2 pMin(center.x - half, center.y - half);
    ImVec2 pMax(center.x + half, center.y + half);

    draw->AddCircleFilled(center, half, IM_COL32(0, 0, 0, 180), 32);

    ImTextureID tex = GetHeroTexture(heroId);
    if (tex)
    {
        draw->AddImageRounded(
            tex,
            pMin,
            pMax,
            ImVec2(0, 0),
            ImVec2(1, 1),
            IM_COL32(255, 255, 255, 255),
            half
        );

        draw->AddCircle(
            center,
            half,
            IM_COL32(255, 0, 0, 235),
            32,
            1.5f
        );
    }
    else
    {
        draw->AddCircleFilled(center, half, IM_COL32(255, 0, 0, 255), 32);
    }
}

void DrawMinimapESP(ImDrawList* draw)
{
    if (!MinimapIcon || !draw)
        return;

    uintptr_t battleManager = GetBattleManager();
    if (!battleManager)
        return;

    uintptr_t showList = ReadPtrEx(battleManager + Offsets::BattleManager::ShowPlayers);
    if (!showList)
        return;

    uintptr_t playerArray = GetListData(showList);
    int playerCount = GetListCount(showList, 20);

    if (!playerArray || playerCount <= 0)
        return;

    for (int i = 0; i < playerCount; i++)
    {
        uintptr_t Objaddr = ReadPtrEx(playerArray + (i * Offsets::List::Stride));
        if (!Objaddr)
            continue;

        if (Read<bool>(Objaddr + Offsets::ShowEntity::SameCampType))
            continue;

        if (Read<bool>(Objaddr + Offsets::ShowEntity::Death))
            continue;

        Vector3 pos{};
        vm_readv(Objaddr + Offsets::ShowEntity::CachePosition, &pos, sizeof(pos));

        if (pos.X == 0.0f && pos.Y == 0.0f && pos.Z == 0.0f)
            continue;

        int heroId = Read<int>(Objaddr + Offsets::ShowEntity::HeroID);
        Vector2 minimapPos = WorldToMinimap(pos);

        DrawMinimapHeroIcon(
            draw,
            ImVec2(minimapPos.X, minimapPos.Y),
            heroId,
            static_cast<float>(g_ICSize)
        );
    }

    if (!HideLine)
    {
        draw->AddRect(
            ImVec2((float)MinimapPos, 0.0f),
            ImVec2((float)(MinimapPos + MinimapSize), (float)MinimapSize),
            IM_COL32(255, 255, 255, 255)
        );
    }
}

//=====================================================================================
// DRAW ESP PLAYER / MONSTER
//=====================================================================================
void DrawMonster(ImDrawList* Draw)
{
    if (!Draw)
        return;

    if (autoRetribution)
    {
        auto bg = ImGui::GetBackgroundDrawList();
        if (bg)
        {
            bg->AddCircleFilled(ImVec2(retriTouchX, retriTouchY), 18.0f, IM_COL32(255, 255, 255, 180), 16);
            bg->AddCircle(ImVec2(retriTouchX, retriTouchY), 18.0f, IM_COL32(0, 0, 0, 255), 16, 2.5f);
        }
    }

    if (abs_ScreenX < abs_ScreenY)
        return;

    uintptr_t battleManager = GetBattleManager();
    if (!battleManager)
        return;

    uintptr_t selfp = ReadPtrEx(battleManager + Offsets::BattleManager::LocalPlayerShow);
    if (!selfp)
        return;

    uintptr_t nativeCamera = GetNativeCamera();
    if (!nativeCamera)
        return;

    Camera ViewMatrix = Read<Camera>(nativeCamera + Offsets::Camera::ViewMatrixOffset);
    _vMatrix = ViewMatrix.projectionMatrix * ViewMatrix.worldToCameraMatrix;

    Vector3 selfPos{};
    vm_readv(selfp + Offsets::ShowEntity::CachePosition, &selfPos, sizeof(selfPos));

    Vector2 selfScreen{};
    bool selfOnScreen = WorldToScreen(selfPos, &selfScreen);

    float lineSize = std::max(1.0f, static_cast<float>(abs_ScreenY) / 432.0f);

    //---------------------------------------------------------------------------------
    // PLAYER / HERO
    //---------------------------------------------------------------------------------
    uintptr_t showPlayers = ReadPtrEx(battleManager + Offsets::BattleManager::ShowPlayers);
    uintptr_t playerArray = GetListData(showPlayers);
    int playerCount = GetListCount(showPlayers, 20);

    if (playerArray && playerCount > 0)
    {
        for (int i = 0; i < playerCount; i++)
        {
            uintptr_t Objaddr = ReadPtrEx(playerArray + (i * Offsets::List::Stride));
            if (!Objaddr)
                continue;

            if (Read<bool>(Objaddr + Offsets::ShowEntity::SameCampType))
                continue;

            if (Read<bool>(Objaddr + Offsets::ShowEntity::Death))
                continue;

            int Health = static_cast<int>(Read<uint64_t>(Objaddr + Offsets::ShowEntity::Hp));
            int maxHealth = static_cast<int>(Read<uint64_t>(Objaddr + Offsets::ShowEntity::HpMax));

            if (Health <= 0 || maxHealth <= 0)
                continue;

            int HeroID = Read<int>(Objaddr + Offsets::ShowEntity::HeroID);

            Vector3 enemyPos{};
            vm_readv(Objaddr + Offsets::ShowEntity::CachePosition, &enemyPos, sizeof(enemyPos));

            Vector2 en_posSc{};
            if (!WorldToScreen(enemyPos, &en_posSc))
                continue;

            Vector2 HeroPos = en_posSc;
            Vector2 drawPos = HeroPos;

            bool isOutScreen =
                HeroPos.X < 0.0f ||
                HeroPos.X > abs_ScreenX ||
                HeroPos.Y < 0.0f ||
                HeroPos.Y > abs_ScreenY;

            float IconSize = static_cast<float>(abs_ScreenX) / 10.4f;

            if (isOutScreen)
            {
                IconSize = static_cast<float>(abs_ScreenX) / 15.6f;
                FindPoint(HeroPos, drawPos, abs_ScreenX, abs_ScreenY, IconSize / 3.0f);
            }

            if (drawMLine && selfOnScreen)
            {
                Draw->AddLine(
                    ImVec2(selfScreen.X, selfScreen.Y),
                    ImVec2(drawPos.X, drawPos.Y),
                    ImColor(255, 255, 255, 255),
                    lineSize
                );
            }

            // Icon hero diperkecil
            if (iconhero)
            {
                DrawHeroIcon(
                    Draw,
                    ImVec2(drawPos.X, drawPos.Y),
                    HeroID,
                    Health,
                    maxHealth,
                    HeroIconRadius
                );
            }

            float textY = drawPos.Y + HeroIconRadius + 6.0f;

            // Hero health bar vertikal, ukuran mengikuti ESP size hero
            if (drawHealthBar)
            {
                float barWidth  = std::max(3.0f, heroEspSize / 8.0f);
                float barHeight = heroEspSize * 3.5f;

                float barX = drawPos.X + HeroIconRadius + 6.0f;

                // Jika terlalu mepet ke kanan, pindahkan ke kiri
                if (barX + barWidth > (float)abs_ScreenX - 2.0f)
                {
                    barX = drawPos.X - HeroIconRadius - 6.0f - barWidth;
                }

                ImVec2 barPos(
                    barX,
                    drawPos.Y - (barHeight / 2.0f)
                );

                DrawHealthBarVertical(
                    Draw,
                    barPos,
                    barWidth,
                    barHeight,
                    (float)Health,
                    (float)maxHealth
                );

                float iconBottomY = drawPos.Y + HeroIconRadius + 6.0f;
                float barBottomY  = barPos.y + barHeight + 4.0f;

                textY = std::max(iconBottomY, barBottomY);
            }

            // Hero distance + name
            if (drawDistance || drawHeroName)
            {
                float Distance = Vector3::Distance(selfPos, enemyPos);

                std::string s;

                if (drawDistance)
                {
                    s += std::to_string((int)Distance);
                    s += "m";
                }

                if (drawHeroName)
                {
                    if (!s.empty())
                        s += " | ";

                    s += fshy(ReadPtrEx(Objaddr + Offsets::ShowPlayer::HeroName));
                }

                if (!s.empty())
                {
                    auto textSize = CalcScaledTextSize(s.c_str(), heroEspSize);

                    DrawEspText(
                        Draw,
                        heroEspSize,
                        drawPos.X - (textSize.x / 2.0f),
                        textY,
                        ImColor(248, 248, 255, 255),
                        s.c_str()
                    );
                }
            }
        }
    }

    //---------------------------------------------------------------------------------
    // MONSTER / MINION / JUNGLE / BOSS
    //---------------------------------------------------------------------------------
    uintptr_t showMonsters = ReadPtrEx(battleManager + Offsets::BattleManager::ShowMonsters);
    uintptr_t monsterArray = GetListData(showMonsters);
    int monsterCount = GetListCount(showMonsters, 128);

    if (!monsterArray || monsterCount <= 0)
        return;

    for (int i = 0; i < monsterCount; i++)
    {
        uintptr_t Objaddr = ReadPtrEx(monsterArray + (i * Offsets::List::Stride));
        if (!Objaddr)
            continue;

        if (Read<bool>(Objaddr + Offsets::ShowEntity::SameCampType))
            continue;

        if (Read<bool>(Objaddr + Offsets::ShowEntity::Death))
            continue;

        int Health = static_cast<int>(Read<uint64_t>(Objaddr + Offsets::ShowEntity::Hp));
        int maxHealth = static_cast<int>(Read<uint64_t>(Objaddr + Offsets::ShowEntity::HpMax));

        if (Health <= 0 || maxHealth <= 0)
            continue;

        int mHeroID = Read<int>(Objaddr + Offsets::ShowEntity::HeroID);
        int type = Read<int>(Objaddr + Offsets::ShowEntity::Type);

        Vector3 monsterPos{};
        vm_readv(Objaddr + Offsets::ShowEntity::CachePosition, &monsterPos, sizeof(monsterPos));

        // Alert Lord / Turtle under attack
        if (drawAlertUnderAttack && type == 5)
        {
            if (mHeroID == 2002 && Health < maxHealth)
            {
                std::string s = "LORD UNDER ATTACK!";
                std::string h = "Health: " + std::to_string(Health);

                auto textSize1 = CalcScaledTextSize(s.c_str(), bossEspSize);
                auto textSize2 = CalcScaledTextSize(h.c_str(), bossEspSize);

                Draw->AddText(
                    nullptr,
                    bossEspSize,
                    ImVec2(abs_ScreenX / 2.0f - textSize1.x / 2.0f, 30.0f),
                    ImColor(248, 248, 255, 255),
                    s.c_str()
                );

                Draw->AddText(
                    nullptr,
                    bossEspSize,
                    ImVec2(abs_ScreenX / 2.0f - textSize2.x / 2.0f, 30.0f + bossEspSize + 4.0f),
                    ImColor(248, 248, 255, 255),
                    h.c_str()
                );
            }

            if (mHeroID == 2003 && Health < maxHealth)
            {
                std::string s = "TURTLE UNDER ATTACK!";
                std::string h = "Health: " + std::to_string(Health);

                auto textSize1 = CalcScaledTextSize(s.c_str(), bossEspSize);
                auto textSize2 = CalcScaledTextSize(h.c_str(), bossEspSize);

                Draw->AddText(
                    nullptr,
                    bossEspSize,
                    ImVec2(abs_ScreenX / 2.0f - textSize1.x / 2.0f, 30.0f),
                    ImColor(248, 248, 255, 255),
                    s.c_str()
                );

                Draw->AddText(
                    nullptr,
                    bossEspSize,
                    ImVec2(abs_ScreenX / 2.0f - textSize2.x / 2.0f, 30.0f + bossEspSize + 4.0f),
                    ImColor(248, 248, 255, 255),
                    h.c_str()
                );
            }
        }

        Vector2 mon_posSc{};
        if (!WorldToScreen(monsterPos, &mon_posSc))
            continue;

        Vector2 MonPos = mon_posSc;

        if (MonPos.X < 0.0f || MonPos.X > abs_ScreenX || MonPos.Y < 0.0f || MonPos.Y > abs_ScreenY)
            continue;

        //-----------------------------------------------------------------------------
        // MINION
        //-----------------------------------------------------------------------------
        if (type == 1)
        {
            if (drawMinionDot)
            {
                ImVec2 minionPos(MonPos.X, MonPos.Y);

                // Outline gelap tipis biar titik tetap kelihatan di background terang
                Draw->AddCircleFilled(
                    minionPos,
                    MinionDotRadius + 1.0f,
                    IM_COL32(0, 0, 0, 200),
                    16
                );

                // Titik merah
                Draw->AddCircleFilled(
                    minionPos,
                    MinionDotRadius,
                    IM_COL32(255, 0, 0, 255),
                    16
                );
            }

            continue;
        }

        //-----------------------------------------------------------------------------
        // LORD / TURTLE / BOSS
        //-----------------------------------------------------------------------------
        if (type == 5)
        {
            std::string bossName = MonsterToString(mHeroID);

            if (bossName.empty())
            {
                if (mHeroID == 2002)
                    bossName = "Lord";
                else if (mHeroID == 2003)
                    bossName = "Turtle";
                else
                    bossName = "Boss";
            }

            float espSize = bossEspSize;

            if (drawHealthBar)
            {
                float barWidth  = espSize * 4.5f;
                float barHeight = std::max(4.0f, espSize / 6.0f);

                ImVec2 barPos(
                    MonPos.X - (barWidth / 2.0f),
                    MonPos.Y - (espSize * 1.4f)
                );

                if (barPos.y < 2.0f)
                    barPos.y = MonPos.Y + (espSize * 1.1f);

                DrawHealthBarHorizontal(
                    Draw,
                    barPos,
                    barWidth,
                    barHeight,
                    (float)Health,
                    (float)maxHealth
                );
            }

            std::string line;

            if (drawMonsterName)
                line += bossName;

            if (drawDistance)
            {
                float DistanceM = Vector3::Distance(selfPos, monsterPos);

                if (!line.empty())
                    line += " | ";

                line += std::to_string((int)DistanceM);
                line += "m";
            }

            if (!line.empty())
            {
                auto textSize = CalcScaledTextSize(line.c_str(), espSize);

                DrawEspText(
                    Draw,
                    espSize,
                    MonPos.X - (textSize.x / 2.0f),
                    MonPos.Y + (espSize * 0.2f),
                    ImColor(255, 100, 100, 255),
                    line.c_str()
                );
            }

            continue;
        }

        //-----------------------------------------------------------------------------
        // JUNGLE MONSTER
        //-----------------------------------------------------------------------------
        if (type == 2)
        {
            if (!bMonster(mHeroID))
                continue;

            std::string monsterName = MonsterToString(mHeroID);

            if (monsterName.empty())
            {
                switch (mHeroID)
                {
                    case 2004:
                        monsterName = "Red Buff";
                        break;

                    case 2005:
                        monsterName = "Blue Buff";
                        break;

                    case 2006:
                        monsterName = "Crab";
                        break;

                    case 2056:
                        monsterName = "Lito";
                        break;

                    default:
                        continue;
                }
            }

            bool isEventMonster = (mHeroID >= 2220 && mHeroID <= 2232);

            ImColor nameColor =
                isEventMonster
                ? ImColor(255, 215, 0, 255)
                : ImColor(220, 180, 255, 255);

            float espSize = monsterEspSize;

            if (drawHealthBar)
            {
                float barWidth  = espSize * 3.5f;
                float barHeight = std::max(3.0f, espSize / 7.0f);

                ImVec2 barPos(
                    MonPos.X - (barWidth / 2.0f),
                    MonPos.Y - (espSize * 1.2f)
                );

                if (barPos.y < 2.0f)
                    barPos.y = MonPos.Y + (espSize * 1.0f);

                DrawHealthBarHorizontal(
                    Draw,
                    barPos,
                    barWidth,
                    barHeight,
                    (float)Health,
                    (float)maxHealth
                );
            }

            std::string line;

            if (drawMonsterName)
            {
                if (isEventMonster)
                    line += "[EVENT] ";

                line += monsterName;
            }

            if (drawDistance)
            {
                float DistanceM = Vector3::Distance(selfPos, monsterPos);

                if (!line.empty())
                    line += " | ";

                line += std::to_string((int)DistanceM);
                line += "m";
            }

            if (!line.empty())
            {
                auto textSize = CalcScaledTextSize(line.c_str(), espSize);

                DrawEspText(
                    Draw,
                    espSize,
                    MonPos.X - (textSize.x / 2.0f),
                    MonPos.Y + (espSize * 0.2f),
                    nameColor,
                    line.c_str()
                );
            }
        }
    }
}

//=====================================================================================
// UI
//=====================================================================================
void Layout_tick_UI()
{
    ImGuiWindowFlags window_flags = ImGuiWindowFlags_AlwaysAutoResize;

    ImGui::SetNextWindowSizeConstraints(ImVec2(800, 0), ImVec2(820, FLT_MAX));
    ImGui::Begin(oxorany("t.me/arsiipkentut"), nullptr, window_flags);

    if (ImGui::BeginTabBar("###MainTabBar"))
    {
        if (ImGui::BeginTabItem(oxorany("ESP")))
        {
            ImGui::Checkbox(oxorany("Line"), &drawMLine);
            ImGui::Checkbox(oxorany("Icon Hero"), &iconhero);
            ImGui::Checkbox(oxorany("Health Bar"), &drawHealthBar);
            ImGui::Checkbox(oxorany("Distance"), &drawDistance);
            ImGui::Checkbox(oxorany("Hero Name"), &drawHeroName);
            ImGui::Checkbox(oxorany("Monster Name"), &drawMonsterName);
            ImGui::Checkbox(oxorany("Alert Lord/Turtle Under Attack"), &drawAlertUnderAttack);

            ImGui::Separator();

            if (ImGui::CollapsingHeader("ESP Setting", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderFloat(oxorany("Hero"), &heroEspSize, 12.0f, 60.0f, "%.1f");
                ImGui::SliderFloat(oxorany("Monster"), &monsterEspSize, 12.0f, 60.0f, "%.1f");
                ImGui::SliderFloat(oxorany("Turtle / Lord"), &bossEspSize, 14.0f, 80.0f, "%.1f");
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Retri")))
        {
            ImGui::Checkbox(oxorany("Auto Retri"), &autoRetribution);

            ImGui::SliderFloat(oxorany("Adjust X"), &retriTouchX, 0.0f, 3000.0f, "%.0f");
            ImGui::SliderFloat(oxorany("Adjust Y"), &retriTouchY, 0.0f, 1500.0f, "%.0f");

            ImGui::Checkbox(oxorany("Buff Red"), &AutoRetributionRed);
            ImGui::Checkbox(oxorany("Buff Blue"), &AutoRetributionBlue);
            ImGui::Checkbox(oxorany("Lord"), &AutoRetributionLord);
            ImGui::Checkbox(oxorany("Turtle"), &AutoRetributionTurtle);
            ImGui::Checkbox(oxorany("Crab"), &AutoRetributionCrab);
            ImGui::Checkbox(oxorany("Lito"), &AutoRetributionLito);

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Minimap")))
        {
            ImGui::Checkbox("Minimap", &MinimapIcon);
            ImGui::SameLine();
            ImGui::Checkbox("Hide Line", &HideLine);

            if (ImGui::CollapsingHeader("Minimap Setting", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::SliderInt("Minimap Size", &MinimapSize, 100, 600);
                ImGui::SliderInt("Minimap Pos X", &MinimapPos, 0, 800);
                ImGui::SliderInt("Icon Size", &g_ICSize, 1, 100);

                ImGui::Text("WorldToMinimap Tweak:");

                ImGui::SliderFloat("Res0 X Mult", &g_Res0_MultX, 0.1f, 3.0f);
                ImGui::SliderFloat("Res0 Y Mult", &g_Res0_MultY, 0.1f, 3.0f);
                ImGui::SliderFloat("Res1 Offset X", &g_Res1_OffsetX, -200.0f, 200.0f);
                ImGui::SliderFloat("Res1 Offset Y", &g_Res1_OffsetY, -200.0f, 200.0f);
                ImGui::SliderFloat("Minimap Scale", &g_MinimapScale, 10.0f, 150.0f);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(oxorany("Settings")))
        {
            static int theme = 0;
            const char* themes[] = { "Dark", "Light", "Classic" };

            if (ImGui::Combo(oxorany("Theme Gui"), &theme, themes, IM_ARRAYSIZE(themes)))
            {
                if (theme == 0)
                    ImGui::StyleColorsDark();
                else if (theme == 1)
                    ImGui::StyleColorsLight();
                else if (theme == 2)
                    ImGui::StyleColorsClassic();
            }

            static float opacity = 1.0f;
            ImGui::SliderFloat(oxorany("UI Opacity"), &opacity, 0.1f, 1.0f);
            ImGui::GetStyle().Alpha = opacity;

            ImGui::Text(oxorany("Current FPS: %.1f"), ImGui::GetIO().Framerate);

            if (ImGui::Button(oxorany("Exit Cheat")))
            {
                main_thread_flag = false;
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (MinimapIcon)
        DrawMinimapESP(ImGui::GetForegroundDrawList());

    DrawMonster(ImGui::GetForegroundDrawList());

    g_window = ImGui::GetCurrentWindow();
    ImGui::End();
}

//=====================================================================================
// MAIN
//=====================================================================================
__attribute__((visibility("default")))
int main(int argc, char* argv[])
{
    pid = pidof(oxorany("com.mobile.legends:UnityKillsMe"));
    g_pid = pid;

    if (pid <= 0)
    {
        printf("MLBB process not found.\n");
        return -1;
    }

    libbase = GetBase(oxorany("libcsharp.so"));
    if (!libbase)
    {
        printf("libcsharp.so base not found.\n");
        return -1;
    }

    printf("Lib: %p\n", reinterpret_cast<void*>(libbase));

    screen_config();

    int screenWidth  = displayInfo.width;
    int screenHeight = displayInfo.height;

    int maxScreen = (screenHeight > screenWidth) ? screenHeight : screenWidth;
    int minScreen = (screenHeight < screenWidth) ? screenHeight : screenWidth;

    ::abs_ScreenX = maxScreen;
    ::abs_ScreenY = minScreen;

    ::native_window_screen_x = maxScreen;
    ::native_window_screen_y = minScreen;

    if (!initGUI_draw(native_window_screen_x, native_window_screen_y, true))
    {
        printf("initGUI_draw failed.\n");
        return -1;
    }

    Touch_Init(displayInfo.width, displayInfo.height, displayInfo.orientation, false);

    ImGui::GetStyle().WindowRounding = 25.0f;

    while (main_thread_flag)
    {
        MonsterRetribution();
        CheckAndTriggerRetribution();

        drawBegin();
        Layout_tick_UI();
        drawEnd();

        usleep(1000);
    }

    shutdown();
    Touch_Close();

    return 0;
}