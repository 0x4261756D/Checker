/**
 * EDOProSE Syntax Checker
 * Copyright (C) 2019  Kevin Lu
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <array>
#include <cstddef>
#include <sqlite3.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <ostream>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>
#include <sys/stat.h>

#include <dirent.h>
#include "ygopro-core/ocgapi.h"
#include "ygopro-core/common.h"
#include "ygopro-core/ocgapi_types.h"

bool verbose = false;
std::string lastScript;
std::vector<OCG_CardData> cardData;
std::map<std::string, std::string> card_files;
std::map<std::string, std::string> special_files;
const std::string prio[] = {"constant.lua", "utility.lua"};

void GetCard([[maybe_unused]] void *payload, uint32_t code, OCG_CardData *card) 
{
    card->code = code;
}

void Log([[maybe_unused]] void *payload, const char *string, int type) 
{
    std::cerr << type << ": " << string << " from " << lastScript << std::endl;
}

static int sqliteCallback([[maybe_unused]]void *NotUsed, int argc, char** argv, char **azColName)
{
    OCG_CardData data;
    for(int i = 0; i < argc; i++)
    {
        std::string name = azColName[i];
        //std::cout << "Here" << name << std::endl;
        if(name == "id")
        {
            data.code = std::atoi(argv[i]);
        }
        else if(name == "ot")
        {
        }
        else if(name == "alias")
        {
            data.alias = std::atoi(argv[i]);
        }
        else if(name == "setcode")
        {
            uint64_t setcodes = std::atoll(argv[i]);
            std::vector<uint16_t> codes;
            for(int j = 0; j < 4; j++)
            {
                uint16_t setcode = (setcodes >> (j * 16)) & 0xffff;
                if(setcode)
                    codes.push_back(setcode);
            }
            if(codes.size())
                codes.push_back(0);
            data.setcodes = codes.data();
        }
        else if(name == "type")
        {
            data.type = std::atoi(argv[i]);
        }
        else if(name == "atk")
        {
            data.attack = std::atoi(argv[i]);
        }
        else if(name == "def")
        {
            data.defense = std::atoi(argv[i]);
            if(data.type & TYPE_LINK)
            {
                data.link_marker = data.defense;
                data.defense = 0;
            }
            else 
            {
                data.link_marker = 0;
            }
        }
        else if(name == "level")
        {
            int level = std::atoi(argv[i]);
            if(level < 0)
                data.level = -(level & 0xff);
            else
                data.level = level & 0xff;
            data.lscale = (level << 24) & 0xff;
            data.rscale = (level << 16) & 0xff;
        }
        else if(name == "race")
        {
            data.race = std::atoi(argv[i]);
        }
        else if(name == "attribute")
        {
            data.attribute = std::atoi(argv[i]);
        }
        else if(name == "category")
        {
        }
    }
    cardData.push_back(data);
    return 0;
}

void LoadRecursive(std::string root)
{
    if(root.find("puzzles") != root.npos)
    {
        std::cout << "omitting puzzles at " << root << std::endl;
        return;
    }
    auto listing = opendir(root.c_str());
    if(!listing)
    {
        throw std::runtime_error("Failed to open " + root);
    }
    for(dirent* entry; (entry = readdir(listing)) != nullptr;)
    {
        if(entry->d_type == DT_DIR && entry->d_name[0] != '.')
        {
            LoadRecursive(root + "/" + entry->d_name);
        }
        else if(entry->d_type == DT_REG)
        {
            std::string name = entry->d_name;
            if(name.rfind(".lua") == name.length() - 4)
            {
                if(name.find_first_of("0123456789") != 1)
                {
                    if(verbose) std::cout << "special file: " << name << " at " << root << std::endl;
                    if(special_files.find(name) == special_files.end())
                    {
                        special_files[name] = root;
                    }
                    else
                    {
                        struct stat result;
                        if(stat((special_files[name] + "/" + name).c_str(), &result) != 0)
                        {
                            throw std::runtime_error("Failed to stat" + special_files[name] + "/" + name);
                        }
                        auto modtime = result.st_mtim;
                        if(stat((root + "/" + name).c_str(), &result))
                        {
                            throw std::runtime_error("Failed to stat" + root + "/" + name);
                        }
                        double i = difftime(modtime.tv_sec, result.st_mtim.tv_sec);
                        if(i < 0)
                        {
                            special_files[name] = root;
                            if(verbose) std::cout << "found newer version of " + name + " in: " + root << std::endl;
                        }
                        else
                            if(verbose) std::cout << "found older version of " + name + " in: " + root << std::endl;
                    }
                }
                else
                {
                    if(card_files.find(name) == card_files.end())
                    {
                        card_files[name] = root;
                    }
                    else
                    {
                        struct stat result;
                        if(stat((card_files[name] + "/" + name).c_str(), &result) != 0)
                        {
                            throw std::runtime_error("Failed to stat" + card_files[name] + "/" + name);
                        }
                        auto modtime = result.st_mtim;
                        if(stat((root + "/" + name).c_str(), &result))
                        {
                            throw std::runtime_error("Failed to stat" + root + "/" + name);
                        }
                        double i = difftime(modtime.tv_sec, result.st_mtim.tv_sec);
                        if(i < 0)
                        {
                            card_files[name] = root;
                            if(verbose) std::cout << "found newer version of " + name + " in: " + root << std::endl;
                        }
                        else
                            if(verbose) std::cout << "found older version of " + name + " in: " + root << std::endl;
                    }
                }
            }
            else if (name.rfind(".cdb") == name.length() - 4)
            {
                sqlite3 *db;
                int res = sqlite3_open((root + "/" + name).c_str(), &db);
                
                if(res)
                {
                    std::cerr << "Can't open database " << sqlite3_errmsg(db) << std::endl;
                    return;
                }
                char *errmsg = 0;
                res = sqlite3_exec(db, "SELECT * FROM datas;", sqliteCallback, 0, &errmsg);

                if(res != SQLITE_OK)
                {
                    std::cerr << "SQL error" << errmsg << std::endl;
                    sqlite3_free(errmsg);
                    
                }
                sqlite3_close(db);
            }
        }
    }
}

void LogCard([[maybe_unused]] void *payload, OCG_CardData *data)
{
    if(verbose)
    std::cout << "alias: " << data->alias << std::endl << "attack: " << data->attack
    << std::endl << "attribute: " << data->attribute << std::endl << "code: " << data->code
    << std::endl << "setcodes: " << data->setcodes << std::endl << "defense: " << data->defense
    << std::endl << "level: " << data->level << std::endl << "link marker: " << data->link_marker
    << std::endl << "lscale: " << data->lscale << std::endl << "rscale: " << data->rscale
    << std::endl << "race: " << data->race << std::endl << "type: " << data->type << std::endl;
}

int ScriptLoad([[maybe_unused]] void *payload, OCG_Duel duel, const char* path)
{
    std::ifstream f;
    f.open(card_files[path] + "/" + path);
    if(!f)
    {
        f.open(special_files[path] + "/" + path);
        if(!f)
        {
            std::cerr << "Failed to open script path " << path << " at " << card_files[path] << std::endl;
            return 1;
        }
    }
    if(verbose) std::cout << "Loading Script at path " + card_files[path] + "/" + path << std::endl;
    std::stringstream buf;
    buf << f.rdbuf();
    lastScript = path;
    return buf.str().length() && OCG_LoadScript(duel, buf.str().c_str(), buf.str().length(), path);
}


void LoadSpecialFiles(OCG_Duel duel)
{
    for(auto &text : prio)
    {
        if(special_files.find(text) != special_files.end())
        {
            ScriptLoad(nullptr, duel, text.c_str());
            special_files.erase(text);
        }
    }
    for(auto it = special_files.begin(); it != special_files.end(); ++it)
    {
        ScriptLoad(nullptr, duel, it->first.c_str());
    }
}

void parseFieldQuery(void* query, const uint32_t length)
{
    std::vector<uint8_t> buffer(static_cast<std::vector<uint8_t>::size_type>(length));
    std::memcpy(buffer.data(), query, static_cast<std::size_t>(length));
    for(auto c = buffer.begin(); c != buffer.end(); ++c)
        std::cout << c.base() << std::endl;
    auto cur = buffer.begin();
    uint32_t options;
    std::memcpy(&options, cur.base(), 1);
    std::cout << options << std::endl;
}

int main(int argc, char* argv[])
{
    if(argc != 3)
    {
        std::cout << "Usage: script_syntax_check <path to scripts> <name of card to test>" << std::endl;
        return 1;
    }
    std::cout << std::stoi(argv[2]) << std::endl;
    OCG_DuelOptions config{};
    config.cardReader = &GetCard;
    config.scriptReader = &ScriptLoad;
    config.logHandler = &Log;
    config.cardReaderDone = &LogCard;
    OCG_Duel duel;
    if(OCG_CreateDuel(&duel, config) != OCG_DUEL_CREATION_SUCCESS)
    {
        std::cout << "Failed to create duel instance!" << std::endl;
    }
    try 
    {
        LoadRecursive(argv[1]);
        std::cout << "Done loading normal files" << std::endl;
        LoadSpecialFiles(duel);
        std::cout << "Done loading special files" << std::endl;
        for(auto it = card_files.end(); it != card_files.begin(); --it)
        {
            if(it->second == "" || it->first == "") continue;
            try 
            {
                OCG_NewCardInfo card{};
                card.code = std::stoi(it->first.substr(1, it->first.length() - 4));
                card.loc = (card.code == (uint32_t)std::stoi(argv[2])) ? LOCATION_HAND : LOCATION_DECK;
                card.team = card.duelist = card.con = 0;
                card.seq = 1;
                card.pos = POS_FACEUP_ATTACK;
                OCG_DuelNewCard(duel, card);
            }
            catch (const std::invalid_argument& e) 
            {
                std::cerr << "-----------Invalid Arg Error: " << e.what() << ": " << it->first << std::endl;
            }
        }
    } 
    catch (const std::runtime_error& e) 
    {
        std::cerr << "Encountered runtime error" << e.what() << std::endl;
        std::cerr << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    OCG_StartDuel(duel);

    //TODO
    uint32_t l;
    auto field = OCG_DuelQueryField(duel, &l);
    parseFieldQuery(field, l);

    OCG_DestroyDuel(duel);
    return EXIT_SUCCESS;
}
