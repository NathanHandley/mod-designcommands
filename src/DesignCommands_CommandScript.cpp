/*
** Made by Nathan Handley https://github.com/NathanHandley
** AzerothCore 2019 http://www.azerothcore.org/
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of the GNU Affero General Public License as published by the
* Free Software Foundation; either version 3 of the License, or (at your
* option) any later version.
*
* This program is distributed in the hope that it will be useful, but WITHOUT
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
* more details.
*
* You should have received a copy of the GNU General Public License along
* with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Cell.h"
#include "Chat.h"
#include "ScriptMgr.h"
#include "CommandScript.h"
#include "Opcodes.h"
#include "Player.h"
#include "DetourNavMeshQuery.h"

#include <vector>
#include <cstdio>
#include <iostream>
#include <fstream>

using namespace Acore::ChatCommands;
using namespace std;

static float WorldScale = 0.29f;

class OutputFile
{
public:
    void WriteLines(string fileName, vector<string> textRows)
    {
        ofstream outputFile(fileName.c_str());
        for (string text : textRows)
        {
            outputFile << text;
            outputFile << "\n";
        }
        outputFile.close();
    }
};

string ConvertNumberToString(uint32 number)
{
    stringstream stringStreamOutput;
    stringStreamOutput << number;
    return stringStreamOutput.str();
}

class CreatureReference
{
public:
    Creature* CreaturePtr;
    uint32 MapID;
    uint32 Entry;
    string Name;
    string SubName;    
};

static std::list<CreatureReference> creatureReferences;
static bool AllCreaturesFall = false;

class DesignCommands_AllCreatureScripts : public AllCreatureScript
{
public:
    DesignCommands_AllCreatureScripts()
        : AllCreatureScript("DesignCommands_AllCreatureScripts")
    {
    }

    void OnCreatureAddWorld(Creature* creature) override
    {
        Map* curMap = creature->GetMap();

        CreatureReference creatureReference;
        creatureReference.MapID = creature->GetMapId();
        creatureReference.Entry = creature->GetEntry();
        creatureReference.Name = creature->GetName();
        creatureReference.SubName = creature->GetCreatureTemplate()->SubName;
        creatureReference.CreaturePtr = creature;
        creatureReferences.push_back(creatureReference);

        if (AllCreaturesFall == true)
        {
            float outHeight = curMap->GetHeight(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), true, 150);
            LOG_INFO("server.loading", "Creature: {}, Height: {}", creatureReference.Name + "," + creatureReference.SubName, outHeight);
           
            //if (creature->isSwimming() == false)
            //    creature->SetPosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ() + 1, creature->GetOrientation());

            bool isObjectInMap = false;
            int maxStepUps = 10;
            int curStep = 0;
            while (isObjectInMap == false && curStep < maxStepUps)
            {
                float height = curMap->GetHeight(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ(), true, 150);
                if (height < -10000)
                {
                    creature->SetPosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ() + 2.5 * curStep, creature->GetOrientation());
                }
                else
                    isObjectInMap = true;
                curStep++;
            }

            if (creature->isSwimming() == false)
                creature->GetMotionMaster()->MoveFall();

            
        }
    }
};

class DesignCommandsPlayerScript : public PlayerScript
{
public:
    DesignCommandsPlayerScript() : PlayerScript("DesignCommandsPlayerScript") {}

    void OnPlayerMapChanged(Player* /*player*/) override
    {

    }
};

static std::string RoundVal(float value, int places)
{
    // Scale, round, and scale back
    float scale = 100000.0f; // 10^6 for 6 decimal places
    float roundedValue;
    if (value < std::numeric_limits<float>::epsilon() && value > -std::numeric_limits<float>::epsilon())
        roundedValue = 0;
    else
        roundedValue = std::round((value + std::numeric_limits<float>::epsilon()) * scale) / scale;

    std::ostringstream stream;
    stream << std::fixed << std::setprecision(6) << roundedValue;
    return stream.str();
}

class LiquidPlane
{
public:
    float nwCornerX;
    float nwCornerY;
    float topZ;
    float seCornerX;
    float seCornerY;
    float bottomZ;

    LiquidPlane() : nwCornerX(0), nwCornerY(0), topZ(0), seCornerX(0), seCornerY(0), bottomZ(0) {}

    void Reset()
    {
        nwCornerX = 0;
        nwCornerY = 0;
        topZ = 0;
        seCornerX = 0;
        seCornerY = 0;
        bottomZ = 0;
    }

    void ToString()
    {
        LOG_INFO("server.loading", "zoneProperties.AddLiquidPlane(LiquidType.Water, \"t50_sbw1\", {}f, {}f, {}f, {}f, {}f, {}f, LiquidSlantType.NorthHighSouthLow, 250f);",
            RoundVal(nwCornerX, 6),
            RoundVal(nwCornerY, 6),
            RoundVal(seCornerX, 6),
            RoundVal(seCornerY, 6),
            RoundVal(topZ, 6),
            RoundVal(bottomZ, 6));
    }
};

enum LiquidPlaneStep
{
    STEP_0_SOUTH_HEIGHT,
    STEP_1_WEST,
    STEP_2_EAST,
    STEP_3_NORTH_HEIGHT
};

static std::string otherZoneLineCoordinates;
static std::string thisZoneLineCoordinates;
static std::list<LiquidPlane> liquidPlanes;
static LiquidPlaneStep curLiquidPlaneStep = LiquidPlaneStep::STEP_0_SOUTH_HEIGHT;
static LiquidPlane curLiquidPlane;

class DesignCommands_CommandScript : public CommandScript
{
public:
    DesignCommands_CommandScript() : CommandScript("DesignCommands_CommandScript") { }

    ChatCommandTable GetCommands() const
    {
        static ChatCommandTable designCommandTable =
        {
            { "dgps",                   HandleDGPSCommand,                   SEC_MODERATOR,          Console::No  },
            { "zlcapture",              HandleZoneLineCaptureCommand,        SEC_MODERATOR,          Console::No  },
            { "zlwrite",                HandleZoneLineWriteCommand,          SEC_MODERATOR,          Console::No  },
            { "zlstephigh",             HandleZoneLineStepHighCommand,       SEC_MODERATOR,          Console::No  },
            { "zlsteplow",              HandleZoneLineStepLowCommand,        SEC_MODERATOR,          Console::No  },
            { "zlclear",                HandleZoneLineClearCommand,          SEC_MODERATOR,          Console::No  },
            { "lpcapture",              HandleLiquidPlaneNodeCaptureCommand, SEC_MODERATOR,          Console::No  },
            { "lpwrite",                HandleLiquidPlaneWriteCommand,       SEC_MODERATOR,          Console::No  },
            { "lpclear",                HandleLiquidPlaneClearCommand,       SEC_MODERATOR,          Console::No  },
            { "zonecreatureswrite",     HandleWriteZoneCreatures,            SEC_MODERATOR,          Console::No  },
            { "zonecreaturescount",     HandleCountZoneCreatures,            SEC_MODERATOR,          Console::No  },
            { "allcreaturefall",        HandleAllCreatureFall,               SEC_MODERATOR,          Console::No  },
            { "npcdown",                HandleNPCDown,                       SEC_MODERATOR,          Console::No  },
            { "npcup",                  HandleNPCUp,                         SEC_MODERATOR,          Console::No  },
        };

        return designCommandTable;
    }

    static bool HandleNPCUp(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Creature* creature = handler->getSelectedCreature();
        if (creature != nullptr)
        {
            creature->SetPosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ() + 3, creature->GetOrientation());
            creature->GetMotionMaster()->MoveFall();
        }
        return true;
    }

    static bool HandleNPCDown(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Creature* creature = handler->getSelectedCreature();
        if (creature != nullptr)
        {
            creature->SetPosition(creature->GetPositionX(), creature->GetPositionY(), creature->GetPositionZ() - 3, creature->GetOrientation());
            creature->GetMotionMaster()->MoveFall();
        }
        return true;
    }

    static std::string RoundVals(float valueX, float valueY, float valueZ, int places)
    {
        std::ostringstream stream;
        stream << RoundVal(valueX, 6) << "f, " << RoundVal(valueY, 6) << "f, " << RoundVal(valueZ, 6) << "f";
        return stream.str();
    }

    static bool HandleAllCreatureFall(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = handler->GetSession()->GetPlayer();
        if (AllCreaturesFall == false)
            for (auto& creatureReference : creatureReferences)
                if (creatureReference.CreaturePtr != nullptr)
                    if (player->GetMapId() == creatureReference.MapID)
                        creatureReference.CreaturePtr->GetMotionMaster()->MoveFall();
        AllCreaturesFall = !AllCreaturesFall;
        LOG_INFO("server.loading", "= All Creature Fall Toggle {} ===========================================", AllCreaturesFall);
        return true;
    }

    static bool HandleCountZoneCreatures(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = handler->GetSession()->GetPlayer();
        uint32 mapID = player->GetMapId();

        int count = 0;
        LOG_INFO("server.loading", "= Counting Creatures ===================================");
        for (auto& creatureReference : creatureReferences)
        {
            if (player->GetMapId() == creatureReference.MapID)
                count++;
        }
        LOG_INFO("server.loading", "Zone Creature Count: {}", count);

        return true;
    }

    static bool HandleWriteZoneCreatures(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        Player* player = handler->GetSession()->GetPlayer();
        uint32 mapID = player->GetMapId();
       
        LOG_INFO("server.loading", "= Writing Creature Data ===========================================");
        vector<string> outputLines;
        for (auto& creatureReference : creatureReferences)
        {
            if (player->GetMapId() == creatureReference.MapID)
            {
                string outputLine;
                outputLine += creatureReference.Name + "," + creatureReference.SubName + ",";
                outputLine += RoundVal(creatureReference.CreaturePtr->GetPositionZ() / WorldScale, 6) + ",";
                outputLines.push_back(outputLine);
                LOG_INFO("server.loading", outputLine);
            }
        }
        OutputFile outputFile;
        string fileName = ConvertNumberToString(mapID) + ".txt";
        outputFile.WriteLines(fileName, outputLines);
        LOG_INFO("server.loading", "Done writing creatures");

        return true;
    }


    static bool HandleDGPSCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
        {
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        }

        WorldObject* object = handler->getSelectedUnit();

        if (!object && !target)
        {
            return false;
        }

        if (!object && target && target->IsConnected())
        {
            object = target->GetConnectedPlayer();
        }

        if (!object)
        {
            return false;
        }
              
        handler->PSendSysMessage("Real: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX(), 6), RoundVal(object->GetPositionY(), 6), RoundVal(object->GetPositionZ(), 6), 
            RoundVal(object->GetPositionX() / WorldScale, 6), RoundVal(object->GetPositionY() / WorldScale, 6), RoundVal(object->GetPositionZ() / WorldScale, 6));
        handler->PSendSysMessage("High: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX() + 0.5f, 6), RoundVal(object->GetPositionY() + 0.5f, 6), RoundVal(object->GetPositionZ() + 0.5f, 6),
            RoundVal((object->GetPositionX() / WorldScale) + 0.5f, 6), RoundVal((object->GetPositionY() / WorldScale) + 0.5f, 6), RoundVal((object->GetPositionZ() / WorldScale) + 0.5f, 6));
        handler->PSendSysMessage("Low: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX() - 0.5f, 6), RoundVal(object->GetPositionY() - 0.5f, 6), RoundVal(object->GetPositionZ() - 0.5f, 6),
            RoundVal((object->GetPositionX() / WorldScale) - 0.5f, 6), RoundVal((object->GetPositionY() / WorldScale) - 0.5f, 6), RoundVal((object->GetPositionZ() / WorldScale) - 0.5f, 6));
        handler->PSendSysMessage("Orientation: {}f", RoundVal(object->GetOrientation(), 6));

        LOG_INFO("server.loading", "Real: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX(), 6), RoundVal(object->GetPositionY(), 6), RoundVal(object->GetPositionZ(), 6), 
            RoundVal(object->GetPositionX() / WorldScale, 6), RoundVal(object->GetPositionY() / WorldScale, 6), RoundVal(object->GetPositionZ() / WorldScale, 6));
        LOG_INFO("server.loading", "High: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX() + 0.5f, 6), RoundVal(object->GetPositionY() + 0.5f, 6), RoundVal(object->GetPositionZ() + 0.5f, 6),
            RoundVal((object->GetPositionX() / WorldScale) + 0.5f, 6), RoundVal((object->GetPositionY() / WorldScale) + 0.5f, 6), RoundVal((object->GetPositionZ() / WorldScale) + 0.5f, 6));
        LOG_INFO("server.loading", "Low: {}f, {}f, {}f - Unscaled: {}f, {}f, {}f", RoundVal(object->GetPositionX() - 0.5f, 6), RoundVal(object->GetPositionY() - 0.5f, 6), RoundVal(object->GetPositionZ() - 0.5f, 6),
            RoundVal((object->GetPositionX() / WorldScale) - 0.5f, 6), RoundVal((object->GetPositionY() / WorldScale) - 0.5f, 6), RoundVal((object->GetPositionZ() / WorldScale) - 0.5f, 6));
        LOG_INFO("server.loading", "Orientation: {}f", RoundVal(object->GetOrientation(), 6));

        return true;
    }

    static bool HandleLiquidPlaneNodeCaptureCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
        {
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        }

        WorldObject* object = handler->getSelectedUnit();

        if (!object && !target)
        {
            return false;
        }

        if (!object && target && target->IsConnected())
        {
            object = target->GetConnectedPlayer();
        }

        if (!object)
        {
            return false;
        }

        // Save coordinates
        float curX = object->GetPositionX();
        float curY = object->GetPositionY();
        float curZ = object->GetPositionZ();

        if (curLiquidPlaneStep == LiquidPlaneStep::STEP_0_SOUTH_HEIGHT)
        {
            LOG_INFO("server.loading", "Starting new liquid plane, begining with south and low");
            handler->PSendSysMessage("Starting new liquid plane, begining with south and low");
            curLiquidPlane.seCornerX = curX - 0.01f;
            curLiquidPlane.bottomZ = curZ - 0.001f;
            LOG_INFO("server.loading", "Captured low height and south edge, next is west");
            handler->PSendSysMessage("Captured low height and south edge, next is west");
            curLiquidPlaneStep = LiquidPlaneStep::STEP_1_WEST;
        }
        else if (curLiquidPlaneStep == LiquidPlaneStep::STEP_1_WEST)
        {
            curLiquidPlane.nwCornerY = curY;
            LOG_INFO("server.loading", "Captured west, next is east");
            handler->PSendSysMessage("Captured west, next is east");

            curLiquidPlaneStep = LiquidPlaneStep::STEP_2_EAST;
        }
        else if (curLiquidPlaneStep == LiquidPlaneStep::STEP_2_EAST)
        {
            curLiquidPlane.seCornerY = curY;
            LOG_INFO("server.loading", "Captured east, next is north + height");
            handler->PSendSysMessage("Captured east, next is north + height");
            curLiquidPlaneStep = LiquidPlaneStep::STEP_3_NORTH_HEIGHT;
        }
        else if (curLiquidPlaneStep == LiquidPlaneStep::STEP_3_NORTH_HEIGHT)
        {
            curLiquidPlane.nwCornerX = curX;
            curLiquidPlane.topZ = curZ;
            liquidPlanes.push_back(curLiquidPlane);
            curLiquidPlane.Reset();
            curLiquidPlane.seCornerX = curX - 0.01f;
            curLiquidPlane.bottomZ = curZ - 0.001f;
            LOG_INFO("server.loading", "Captured north and high height for current plane, south and low for next plane. Next is west.");
            handler->PSendSysMessage("Captured north and high height for current plane, south and low for next plane. Next is west.");
            curLiquidPlaneStep = LiquidPlaneStep::STEP_1_WEST;
        }

        return true;
    }

    static bool HandleLiquidPlaneWriteCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        LOG_INFO("server.loading", " == Writing Planes == ");
        for (auto& waterPlane : liquidPlanes)
            waterPlane.ToString();
        return true;
    }

    static bool HandleLiquidPlaneClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        curLiquidPlaneStep = LiquidPlaneStep::STEP_0_SOUTH_HEIGHT;
        curLiquidPlane.Reset();
        liquidPlanes.clear();
        LOG_INFO("server.loading", " == Planes Cleared == ");
        return true;
    }

    static bool HandleZoneLineCaptureCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
        {
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        }

        WorldObject* object = handler->getSelectedUnit();

        if (!object && !target)
        {
            return false;
        }

        if (!object && target && target->IsConnected())
        {
            object = target->GetConnectedPlayer();
        }

        if (!object)
        {
            return false;
        }

        // Save coordinates
        float curX = object->GetPositionX();
        float curY = object->GetPositionY();
        float curZ = object->GetPositionZ();

        ////**************
        // Qeynos2 (North Qeynos) is to the south
        // Strings
        std::string thisZoneName = "qeynos2";
        std::string thisZonePortInOrientation = "ZoneLineOrientationType.South";

        std::string otherZoneName = "qeytoqrg";
        std::string otherZonePortInOrientation = "ZoneLineOrientationType.North";

        // Working coordinates
        float thisZoneCurX = curX;
        float thisZoneCurY = curY;
        float thisZoneCurZ = curZ;

        float otherZoneCurX = -310.758850f;
        float otherZoneCurY = curY + 0.439697f;
        float otherZoneCurZ = -7.843740f;

        // Box Containers
        float thisZoneBoxTopX = thisZoneCurX + 50.0f;
        float thisZoneBoxBottomX = thisZoneCurX + 20.0f;

        float thisZoneBoxTopY = thisZoneCurY + 10.0f;
        float thisZoneBoxBottomY = thisZoneCurY - 10.0f;

        float thisZoneBoxTopZ = 200.0f;
        float thisZoneBoxBottomZ = -100.00f;

        float otherZoneBoxTopX = otherZoneCurX - 20.0f;
        float otherZoneBoxBottomX = otherZoneCurX - 50.0f;

        float otherZoneBoxTopY = otherZoneCurY + 10.0f;
        float otherZoneBoxBottomY = otherZoneCurY - 10.0f;

        float otherZoneBoxTopZ = 0;
        float otherZoneBoxBottomZ = 0;

        // Generate the strings
        std::string thisZoneBoxTopString = RoundVals(thisZoneBoxTopX, thisZoneBoxTopY, thisZoneBoxTopZ, 6);
        std::string thisZoneBoxBottomString = RoundVals(thisZoneBoxBottomX, thisZoneBoxBottomY, thisZoneBoxBottomZ, 6);
        std::string otherZoneTargetPosition = RoundVals(otherZoneCurX, otherZoneCurY, otherZoneCurZ, 6);
        std::ostringstream thisZoneLineStream;
        thisZoneLineStream << thisZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + otherZoneName + "\", " + otherZoneTargetPosition + ", " + otherZonePortInOrientation + ", " + thisZoneBoxTopString + ", " + thisZoneBoxBottomString + "); \n";
        thisZoneLineCoordinates = thisZoneLineStream.str();

        std::string otherZoneBoxTopString = RoundVals(otherZoneBoxTopX, otherZoneBoxTopY, otherZoneBoxTopZ, 6);
        std::string otherZoneBoxBottomString = RoundVals(otherZoneBoxBottomX, otherZoneBoxBottomY, otherZoneBoxBottomZ, 6);
        std::string thisZoneTargetPosition = RoundVals(thisZoneCurX, thisZoneCurY, thisZoneCurZ, 6);
        std::ostringstream otherZoneLineStream;
        otherZoneLineStream << otherZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + thisZoneName + "\", " + thisZoneTargetPosition + ", " + thisZonePortInOrientation + ", " + otherZoneBoxTopString + ", " + otherZoneBoxBottomString + "); \n";
        otherZoneLineCoordinates = otherZoneLineStream.str();
        ////******************



















        /////// Oasis of marr and North Ro
        //// Strings
        //std::string thisZoneName = "oasis";
        //std::string thisZonePortInOrientation = "ZoneLineOrientationType.South";

        //std::string otherZoneName = "nro";
        //std::string otherZonePortInOrientation = "ZoneLineOrientationType.North";

        //// Working coordinates
        //float thisZoneCurX = curX;
        //float thisZoneCurY = curY;
        //float thisZoneCurZ = curZ;

        //float otherZoneCurX = 0;
        //float otherZoneCurY = 0;
        //float otherZoneCurZ = 0;

        //// Box Containers
        //float thisZoneBoxTopX = thisZoneCurX - 20.0f;
        //float thisZoneBoxTopY = thisZoneCurY + 10.0f;
        //float thisZoneBoxTopZ = 300.0f;
        //float thisZoneBoxBottomX = thisZoneCurX + 50.0f;
        //float thisZoneBoxBottomY = thisZoneCurY - 10.0f;
        //float thisZoneBoxBottomZ = -200.00f;

        //float otherZoneBoxTopX = 0;
        //float otherZoneBoxTopY = 0;
        //float otherZoneBoxTopZ = 0;
        //float otherZoneBoxBottomX = 0;
        //float otherZoneBoxBottomY = 0;
        //float otherZoneBoxBottomZ = 0;

        //// Generate the strings
        //std::string thisZoneBoxTopString = RoundVals(thisZoneBoxTopX, thisZoneBoxTopY, thisZoneBoxTopZ, 6);
        //std::string thisZoneBoxBottomString = RoundVals(thisZoneBoxBottomX, thisZoneBoxBottomY, thisZoneBoxBottomZ, 6);
        //std::string otherZoneTargetPosition = RoundVals(otherZoneCurX, otherZoneCurY, otherZoneCurZ, 6);
        //std::ostringstream thisZoneLineStream;
        //thisZoneLineStream << thisZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + otherZoneName + "\", " + otherZoneTargetPosition + ", " + otherZonePortInOrientation + ", " + thisZoneBoxTopString + ", " + thisZoneBoxBottomString + "); \n";
        //thisZoneLineCoordinates = thisZoneLineStream.str();

        //std::string otherZoneBoxTopString = RoundVals(otherZoneBoxTopX, otherZoneBoxTopY, otherZoneBoxTopZ, 6);
        //std::string otherZoneBoxBottomString = RoundVals(otherZoneBoxBottomX, otherZoneBoxBottomY, otherZoneBoxBottomZ, 6);
        //std::string thisZoneTargetPosition = RoundVals(thisZoneCurX, thisZoneCurY, thisZoneCurZ, 6);
        //std::ostringstream otherZoneLineStream;
        //otherZoneLineStream << otherZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + thisZoneName + "\", " + thisZoneTargetPosition + ", " + thisZonePortInOrientation + ", " + otherZoneBoxTopString + ", " + otherZoneBoxBottomString + "); \n";
        //otherZoneLineCoordinates = otherZoneLineStream.str();

        /////// ------
        //// NRO - EFP, zones are north (EFP) and south (NPO) of one another
        //// Strings
        //std::string thisZoneName = "nro";
        //std::string thisZonePortInOrientation = "ZoneLineOrientationType.South";

        //std::string otherZoneName = "freporte";
        //std::string otherZonePortInOrientation = "ZoneLineOrientationType.North";

        //// Working coordinates
        //float thisZoneCurX = curX;
        //float thisZoneCurY = curY;
        //float thisZoneCurZ = curZ;

        //float otherZoneCurX = -1316.303711f;
        //float otherZoneCurY = thisZoneCurY - 1033.602013f;
        //float otherZoneCurZ = -55.968739f;

        //// Box Containers
        //float thisZoneBoxTopX = thisZoneCurX + 50.0f;
        //float thisZoneBoxTopY = thisZoneCurY + 10.0f;
        //float thisZoneBoxTopZ = 200.0f;
        //float thisZoneBoxBottomX = thisZoneCurX + 20.0f;
        //float thisZoneBoxBottomY = thisZoneCurY - 10.0f;
        //float thisZoneBoxBottomZ = -100.00f;

        //float otherZoneBoxTopX = otherZoneCurX - 20.0f;
        //float otherZoneBoxTopY = otherZoneCurY + 10.0f;
        //float otherZoneBoxTopZ = 200.0f;
        //float otherZoneBoxBottomX = otherZoneCurX - 50.0f;
        //float otherZoneBoxBottomY = otherZoneCurY - 10.0f;
        //float otherZoneBoxBottomZ = -100.00f;

        //// Generate the strings
        //std::string thisZoneBoxTopString = RoundVals(thisZoneBoxTopX, thisZoneBoxTopY, thisZoneBoxTopZ, 6);
        //std::string thisZoneBoxBottomString = RoundVals(thisZoneBoxBottomX, thisZoneBoxBottomY, thisZoneBoxBottomZ, 6);
        //std::string otherZoneTargetPosition = RoundVals(otherZoneCurX, otherZoneCurY, otherZoneCurZ, 6);
        //std::ostringstream thisZoneLineStream;
        //thisZoneLineStream << thisZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + otherZoneName + "\", " + otherZoneTargetPosition + ", " + otherZonePortInOrientation + ", " + thisZoneBoxTopString + ", " + thisZoneBoxBottomString + "); \n";
        //thisZoneLineCoordinates = thisZoneLineStream.str();

        //std::string otherZoneBoxTopString = RoundVals(otherZoneBoxTopX, otherZoneBoxTopY, otherZoneBoxTopZ, 6);
        //std::string otherZoneBoxBottomString = RoundVals(otherZoneBoxBottomX, otherZoneBoxBottomY, otherZoneBoxBottomZ, 6);
        //std::string thisZoneTargetPosition = RoundVals(thisZoneCurX, thisZoneCurY, thisZoneCurZ, 6);
        //std::ostringstream otherZoneLineStream;
        //otherZoneLineStream << otherZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + thisZoneName + "\", " + thisZoneTargetPosition + ", " + thisZonePortInOrientation + ", " + otherZoneBoxTopString + ", " + otherZoneBoxBottomString + "); \n";
        //otherZoneLineCoordinates = otherZoneLineStream.str();


        ////**************
        //// This is a backup of West Freeport - East Commons  (Example of east/west)
        //// Strings
        //std::string thisZoneName = "ecommons";
        //std::string thisZonePortInOrientation = "ZoneLineOrientationType.West";

        //std::string otherZoneName = "freportw";
        //std::string otherZonePortInOrientation = "ZoneLineOrientationType.East";

        //// Working coordinates
        //float thisZoneCurX = curX;
        //float thisZoneCurY = curY;
        //float thisZoneCurZ = curZ;

        //float otherZoneCurX = thisZoneCurX + 0.772155f;
        //float otherZoneCurY = 791.873230f;
        //float otherZoneCurZ = -27.999950f;

        //// Box Containers
        //float thisZoneBoxTopX = thisZoneCurX + 10.0f;
        //float thisZoneBoxTopY = thisZoneCurY - 20.0f;
        //float thisZoneBoxTopZ = 200.0f;
        //float thisZoneBoxBottomX = thisZoneCurX - 10.0f;
        //float thisZoneBoxBottomY = thisZoneCurY - 50.0f;
        //float thisZoneBoxBottomZ = -100.00f;

        //float otherZoneBoxTopX = otherZoneCurX + 10.0f;
        //float otherZoneBoxTopY = otherZoneCurY + 50.0f;
        //float otherZoneBoxTopZ = 200.0f;
        //float otherZoneBoxBottomX = otherZoneCurX - 10.0f;
        //float otherZoneBoxBottomY = otherZoneCurY + 20.0f;
        //float otherZoneBoxBottomZ = -100.00f;

        //// Generate the strings
        //std::string thisZoneBoxTopString = RoundVals(thisZoneBoxTopX, thisZoneBoxTopY, thisZoneBoxTopZ, 6);
        //std::string thisZoneBoxBottomString = RoundVals(thisZoneBoxBottomX, thisZoneBoxBottomY, thisZoneBoxBottomZ, 6);
        //std::string otherZoneTargetPosition = RoundVals(otherZoneCurX, otherZoneCurY, otherZoneCurZ, 6);
        //std::ostringstream thisZoneLineStream;
        //thisZoneLineStream << thisZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + otherZoneName + "\", " + otherZoneTargetPosition + ", " + otherZonePortInOrientation + ", " + thisZoneBoxTopString + ", " + thisZoneBoxBottomString + "); \n";
        //thisZoneLineCoordinates = thisZoneLineStream.str();

        //std::string otherZoneBoxTopString = RoundVals(otherZoneBoxTopX, otherZoneBoxTopY, otherZoneBoxTopZ, 6);
        //std::string otherZoneBoxBottomString = RoundVals(otherZoneBoxBottomX, otherZoneBoxBottomY, otherZoneBoxBottomZ, 6);
        //std::string thisZoneTargetPosition = RoundVals(thisZoneCurX, thisZoneCurY, thisZoneCurZ, 6);
        //std::ostringstream otherZoneLineStream;
        //otherZoneLineStream << otherZoneLineCoordinates << "zoneProperties.AddZoneLineBox(\"" + thisZoneName + "\", " + thisZoneTargetPosition + ", " + thisZonePortInOrientation + ", " + otherZoneBoxTopString + ", " + otherZoneBoxBottomString + "); \n";
        //otherZoneLineCoordinates = otherZoneLineStream.str();
        //////******************

        return true;
    }

    static bool HandleZoneLineWriteCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        LOG_INFO("server.loading", "");
        LOG_INFO("server.loading", "{}", thisZoneLineCoordinates);
        LOG_INFO("server.loading", "{}", otherZoneLineCoordinates);
        return true;
    }

    static bool HandleZoneLineStepHighCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
        {
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        }

        WorldObject* object = handler->getSelectedUnit();

        if (!object && !target)
        {
            return false;
        }

        if (!object && target && target->IsConnected())
        {
            object = target->GetConnectedPlayer();
        }

        if (!object)
        {
            return false;
        }

        // Calc new position
        float newX = object->GetPositionX() + 20.0f;
        float newY = object->GetPositionY();
        float newZ = object->GetPositionZ() + 30.0f;

        Player* player = handler->GetSession()->GetPlayer();
        player->TeleportTo({ player->GetMapId(), {newX, newY, newZ, player->GetOrientation()} });
        return true;
    }

    static bool HandleZoneLineStepLowCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target)
        {
            target = PlayerIdentifier::FromTargetOrSelf(handler);
        }

        WorldObject* object = handler->getSelectedUnit();

        if (!object && !target)
        {
            return false;
        }

        if (!object && target && target->IsConnected())
        {
            object = target->GetConnectedPlayer();
        }

        if (!object)
        {
            return false;
        }

        // Calc new position
        float newX = object->GetPositionX();
        float newY = object->GetPositionY() - 20.0f;
        float newZ = object->GetPositionZ();

        Player* player = handler->GetSession()->GetPlayer();
        player->TeleportTo({ player->GetMapId(), {newX, newY, newZ, player->GetOrientation()} });
        return true;
    }

    static bool HandleZoneLineClearCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        thisZoneLineCoordinates = "";
        otherZoneLineCoordinates = "";
        return true;
    }
};

void AddDesignCommandsCommandScripts()
{
    new DesignCommands_CommandScript();
}

void AddDesignCommandsAllCreatureScripts()
{
    new DesignCommands_AllCreatureScripts();
}

void AddDesignCommandsPlayerScript()
{
    new DesignCommandsPlayerScript();
}

