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
#include "ygopro-core/card.h"
#include "ygopro-core/ocgapi.h"
#include "ygopro-core/common.h"
#include "ygopro-core/ocgapi_types.h"

bool verbose = false;
std::string lastScript;
std::vector<OCG_CardData> cardData;
std::map<std::string, std::string> card_files;
std::map<std::string, std::string> special_files;
const std::string prio[] = {"constant.lua", "utility.lua"};

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

void LogLoc(loc_info loc)
{
	std::cout << "Controller: " << +loc.controler << std::endl;
	std::cout << "Location: " << +loc.location << std::endl;
	std::cout << "Sequence: " << loc.sequence << std::endl;
	std::cout << "Position: " << loc.position << std::endl;
}

void GetCard([[maybe_unused]] void *payload, uint32_t code, OCG_CardData *card) 
{
	card->code = code;
	for(auto i = cardData.begin(); i != cardData.end(); ++i)
	{
		if(i->code == code)
		{
			card = i.base();
			return;
		}
	}
	std::cout << "couldn't find " << code << "in card_data" << std::endl;
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

template<typename T> T getAtPos(uint8_t* buffer, uint32_t* index, uint32_t length, bool* success)
{
	T val;
	std::memcpy(&val, (buffer + *index), sizeof(val));
	*index += sizeof(val);
	*success = *index > length;
	return val;
}

// used ocgapi.cpp OCG_DuelQueryField as reference
void parseFieldQuery(void* query, const uint32_t length)
{
	unsigned char* buffer = reinterpret_cast<unsigned char*>(query);
	bool failure = false;
	uint32_t counter = 0;
	int32_t options = getAtPos<uint32_t>(buffer, &counter, length, &failure);
	std::cout << "Duel Options: " << options << std::endl;
	if(failure) return;
	//for each player
	for(int i = 0; i < 2; ++i)
	{
		std::cout << "------PLAYER " << i << "------" << std::endl;
		int32_t lp = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		std::cout << "LP: " << lp << std::endl;
		if(failure) return;
		//for each spot in the mzone
		std::cout << "---MONSTER CARDS---" << std::endl;
		for(int j = 0; j < 7; ++j)
		{
			int8_t exists = getAtPos<uint8_t>(buffer, &counter, length, &failure);
			std::cout << "Card " << j;
			if(failure) return;
			if(exists)
			{
				int8_t position = getAtPos<uint8_t>(buffer, &counter, length, &failure);
				if(failure) return;
				int32_t xyz_mat = getAtPos<uint32_t>(buffer, &counter, length, &failure);
				if(failure) return;
				std::cout << " is at pos " << position << " and has " << xyz_mat << "materials." << std::endl;
			}
			else
			{
				std::cout << " does not exist" <<std::endl;
			}
		}
		std::cout << "---S/T CARDS---" << std::endl;
		//for each spot in the szone
		for(int j = 0; j < 8; ++j)
		{
			int8_t exists = getAtPos<uint8_t>(buffer, &counter, length, &failure);
			if(failure) return;
			std::cout << "Card " << j;
			if(exists)
			{
				int8_t position = getAtPos<uint8_t>(buffer, &counter, length, &failure);
				if(failure) return;
				int32_t xyz_mat = getAtPos<uint32_t>(buffer, &counter, length, &failure);
				if(failure) return;
				std::cout << " is at pos " << position << " and has " << xyz_mat << "materials." << std::endl;
			}
			else
			{
				std::cout << " does not exist" <<std::endl;
			}
		}
		int32_t main_deck = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Main Deck size: " << main_deck << std::endl;
		int32_t hand = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Hand size: " << hand << std::endl;
		int32_t grave = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Grave size: " << grave << std::endl;
		int32_t remove = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Banish Pile size: " << remove << std::endl;
		int32_t extra_deck = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Extra Deck size: " << extra_deck << std::endl;
		int32_t extra_p_count = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Faceup Extra Deck count: " << extra_p_count << std::endl;
	}
	std::cout << "------CHAIN------" <<std::endl;
	int32_t chain_size = getAtPos<uint32_t>(buffer, &counter, length, &failure);
	if(failure) return;
	std::cout << "Chain size: " << chain_size << std::endl;
	for(int32_t i = 0; i < chain_size; i++)
	{
		int32_t triggering_effect = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Triggering effect: " << triggering_effect << std::endl;
		int8_t controller = getAtPos<uint8_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Controller: " << controller << std::endl;
		int8_t location = getAtPos<uint8_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Location: " << location << std::endl;
		int32_t sequence = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Sequence: " << sequence << std::endl;
		int32_t position = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Position: " << position << std::endl;
		int8_t triggering_controller = getAtPos<uint8_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Triggering Controller: " << triggering_controller << std::endl;
		int8_t triggering_location = getAtPos<uint8_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Triggering Location: " << triggering_location << std::endl;
		int32_t triggering_sequence = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Triggering Sequence: " << triggering_sequence << std::endl;
		int64_t description = getAtPos<uint64_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << "Description: " << description << std::endl;
	}
	std::cout << counter << ":" << length << std::endl;
}

uint32_t getInSize(uint8_t* buffer, uint32_t* index, uint32_t length, bool* success, uint16_t size)
{
	uint32_t val;
	std::memcpy(&val, buffer + *index, size);
	*index += size;
	*success = *index > length;
	return val;
}

// used card.cpp card::get_infos as reference
void parseQuery(void* query, const uint32_t length)
{
	uint8_t* buffer = reinterpret_cast<uint8_t*>(query);
	bool failure = true;
	uint32_t counter = 0;
	OCG_CardData card;
	uint32_t position;
	uint32_t rank;
	uint32_t base_attack, base_defense;
	uint32_t reason;
	uint32_t cover;
	loc_info reason_loc, equip_loc;
	std::vector<loc_info> target_locs;
	std::vector<uint32_t> overlay_cards, counters;
	uint8_t owner = 0;
	uint32_t status;
	uint8_t is_public = 0, is_hidden = 0;
	uint32_t get_link;
	while(counter < length)
	{
		uint16_t size = getAtPos<uint16_t>(buffer, &counter, length, &failure) - sizeof(uint32_t);
		if(failure) return;
		uint32_t query_type = getAtPos<uint32_t>(buffer, &counter, length, &failure);
		if(failure) return;
		std::cout << size << ", " << query_type << std::endl;
		switch (query_type) 
		{
			case QUERY_CODE:
				card.code = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_POSITION:
				position = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_ALIAS:
				card.alias = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_TYPE:
				card.type = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_LEVEL:
				card.level = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_RANK:
				rank = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_ATTRIBUTE:
				card.attribute = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_RACE:
				card.race = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_ATTACK:
				card.attack = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_DEFENSE:
				card.defense = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_BASE_ATTACK:
				base_attack = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_BASE_DEFENSE:
				base_defense = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_REASON:
				reason = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_COVER:
				cover = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_REASON_CARD:
				reason_loc.controler = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
				reason_loc.location = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
				reason_loc.sequence = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
				reason_loc.position = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
			break;
			case QUERY_EQUIP_CARD:
				equip_loc.controler = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
				equip_loc.location = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
				equip_loc.sequence = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
				equip_loc.position = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
			break;
			case QUERY_TARGET_CARD:
				for(uint32_t c = 0; c < getInSize(buffer, &counter, length, &failure, sizeof(uint32_t)); c += sizeof(uint8_t) * 2 + sizeof(uint32_t) * 2)
				{
					loc_info info;
					info.controler = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
					info.location = getInSize(buffer, &counter, length, &failure, sizeof(uint8_t));
					info.sequence = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
					info.position = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
					target_locs.push_back(info);
				}
			break;
			case QUERY_OVERLAY_CARD:
				for(uint32_t c = 0; c < getInSize(buffer, &counter, length, &failure, sizeof(uint32_t)); c += sizeof(uint32_t))
				{
					overlay_cards.push_back(getInSize(buffer, &counter, length, &failure, sizeof(uint32_t)));
				}
			break;
			case QUERY_COUNTERS:
				for(uint32_t c = 0; c < getInSize(buffer, &counter, length, &failure, sizeof(uint32_t)); c += sizeof(uint32_t))
				{
					counters.push_back(getInSize(buffer, &counter, length, &failure, sizeof(uint32_t)));
				}
			break;
			case QUERY_OWNER:
				owner = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_STATUS:
				status = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_IS_PUBLIC:
				is_public = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_LSCALE:
				card.lscale = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_RSCALE:
				card.rscale = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_LINK:
				get_link = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
				card.link_marker = getInSize(buffer, &counter, length, &failure, sizeof(uint32_t));
			break;
			case QUERY_IS_HIDDEN:
				is_hidden = getInSize(buffer, &counter, length, &failure, size);
			break;
			case QUERY_END:

			break;
			default:
				std::cout << "UNDEFINED, RETURNING" << std::endl;
				failure = true;
		}
		if(failure) return;         
	}
	std::cout << "------CARD------" << std::endl;
	bool tmp = verbose;
	verbose = true;
	LogCard(nullptr, &card);
	verbose = tmp;
	std::cout << "Additional data: " << std::endl;
	std::cout << "Position: " << position << std::endl;
	std::cout << "Rank: " << rank << std::endl;
	std::cout << "Base Attack: " << base_attack << std::endl;
	std::cout << "Base Defense: " << base_defense << std::endl;
	std::cout << "Cover: " << cover << std::endl;
	std::cout << "Reason: " << reason << std::endl;
	std::cout << "Reason Location Info: " << std::endl;
	LogLoc(reason_loc);
	std::cout << "Equip Location Info: " << std::endl;
	LogLoc(equip_loc);
	std::cout << "Target Location Info: " << std::endl;
	for(auto i = target_locs.begin(); i != target_locs.end(); ++i)
	{
		LogLoc(*i);
	}
	std::cout << "Overlay Cards: " << std::endl;
	for(auto i = overlay_cards.begin(); i != overlay_cards.end(); ++i)
	{
		std::cout << *i << std::endl;
	}
	std::cout << "Counters: " << std::endl;
	for(auto i = counters.begin(); i != counters.end(); ++i)
	{
		std::cout << *i << std::endl;
	}
	std::cout << "Get Link: " << get_link << std::endl;
	std::cout << "Owner: " << +owner << std::endl;
	std::cout << "Status: " << status << std::endl;
	std::cout << "Is Public: " << +is_public << std::endl;
	std::cout << "Is Hidden: " << +is_hidden << std::endl;
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
	OCG_Player team1;
	team1.startingLP = 12345678;
	team1.startingDrawCount = 0;
	OCG_Player team2;
	team2.startingLP = 15;
	team2.startingDrawCount = 0;
	config.team1 = team1;
	config.team2 = team2;
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
	OCG_QueryInfo info;
	info.loc = LOCATION_HAND;
	info.seq = 0;
	info.flags = 0xfffffff;
	auto card = OCG_DuelQuery(duel, &l, info);
	std::cout << l << std::endl;
	parseQuery(card, l);
	std::cout << "Log" <<std::endl;
	OCG_CardData data;
	GetCard(nullptr, std::stoi(argv[2]), &data);
	LogCard(nullptr, &data);
	OCG_DestroyDuel(duel);
	return EXIT_SUCCESS;
}
