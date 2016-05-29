/*
 * plugin-chanops.cpp
 *
 *  Created on: 03 Aug 2011
 *      Author: oaskivvy@gmail.com
 */

/*-----------------------------------------------------------------.
| Copyright (C) 2011 SooKee oaskivvy@gmail.com                     |
'------------------------------------------------------------------'

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301, USA.

http://www.gnu.org/licenses/gpl-2.0.html

'-----------------------------------------------------------------*/

#include <skivvy/plugin-chanops.h>

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

#include <sookee/str.h>
#include <sookee/types/basic.h>
#include <sookee/types/vec.h>

#include <skivvy/logrep.h>
#include <sookee/stl.h>
#include <sookee/ios.h>
#include <skivvy/irc-constants.h>
#include <skivvy/irc.h>
#include <skivvy/utils.h>

namespace skivvy { namespace ircbot {

IRC_BOT_PLUGIN(ChanopsIrcBotPlugin);
PLUGIN_INFO("chanops", "Channel Operations", "2.0-alpha");

using namespace skivvy;
using namespace sookee;
using namespace skivvy::irc;
using namespace sookee::types;
using namespace skivvy::utils;
using namespace sookee::utils;

const str STORE_FILE_KEY = "chanops.store.file";
const str STORE_FILE_VAL = "chanops-store.txt"; // default

const str CHANOPS_CHANNEL_KEY = "chanops.channel";

const str IRCUSER_FILE_KEY = "chanops.ircuser.file";
const str IRCUSER_FILE_VAL = "chanops-ircuser-db.txt"; // default

//====================================================
// ChanopsChannel
//====================================================

ChanopsChannel::ChanopsChannel(IrcBot& bot, ChanopsIrcBotPlugin& plugin, const str& channel)
: bot(bot), plugin(plugin), channel(channel)
{
}

str channel_to_filename(str channel)
{
	return replace(replace(channel, "#", "H-"), "@", "A-");
}

bool ChanopsChannel::init()
{
	auto cfg_dir = bot.get_data_folder() + '/' + channel_to_filename(channel);

	std::error_code ec;
	fs::create_directories(cfg_dir, ec);
	if(ec)
	{
		log("E: " << ec.message());
		return false;
	}
	auto store_name = cfg_dir + '/' + bot.get(STORE_FILE_KEY, STORE_FILE_VAL);
	store = std::make_unique<BackupStore>(store_name);
	return true;
}

void ChanopsChannel::exit()
{
}

void ChanopsChannel::cookie(const message& msg, int num)
{
	assert(msg.get_chan() == channel);
	BUG_COMMAND(msg);
	bug_var(num);
	bug_var(this->channel);

	str nick = msg.get_user_params();

	if(num)
	{
		store->set("cookies." + nick, store->get("cookies." + nick, 0) + num);
		if(num < 0)
			bot.fc_reply(msg, REPLY_PROMPT + msg.get_nickname() + " has taken "
				+ std::to_string(-num) + " cookie" + (-num>1?"s":"") + " from " + nick + ".");
		else
			bot.fc_reply(msg, REPLY_PROMPT + msg.get_nickname() + " has given "
				+ std::to_string(num) + " cookie" + (num>1?"s":"") + " to " + nick + ".");
		return;
	}

	int n = store->get("cookies." + nick, 0);
	bot.fc_reply(msg, REPLY_PROMPT + nick + " has " + std::to_string(n) + " cookie" + ((n==1||n==-1)?"":"s"));
}

// Delegated Plugin API

void ChanopsChannel::event(const message& msg)
{
	bug_fun();
	bug_var(this->channel);
	(void) msg;
}

//====================================================
// BasicIrcBotPlugin
//====================================================

ChanopsIrcBotPlugin::ChanopsIrcBotPlugin(IrcBot& bot)
: BasicIrcBotPlugin(bot)
{
}

// Plugin API
str_vec ChanopsIrcBotPlugin::api(unsigned call, const str_vec& args)
{
	(void) call;
	(void) args;
	return {};
}

// INTERFACE: BasicIrcBotPlugin

bool ChanopsIrcBotPlugin::initialize()
{
	bug_fun();

	// initilize one ChanopsChannel per channel
	for(auto&& chan: bot.get_vec(CHANOPS_CHANNEL_KEY))
	{
		auto found = insts.find(chan);

		if(found != insts.end())
		{
			log("E: config has duplicate Chanops for channel: " << chan);
			continue;
		}

		ChanopsApi::UPtr uptr = std::make_unique<ChanopsChannel>(bot, *this, chan);

		if(!uptr->init())
		{
			log("E: initializing Chanops for channel: " << chan);
			continue;
		}

		insts[chan] = std::move(uptr);
	}

	add
	({
		"!cookies"
		, "!cookies <nick> Show <nick>'s cookies."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan())->cookie(msg, 0);}catch(...){}
		}
	});
	add
	({
		"!cookie++"
		, "!cookie++ <nick> Give <nick> a cookie."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan())->cookie(msg, 1);}catch(...){}
		}
	});
	add
	({
		"!cookie--"
		, "!cookie-- <nick> Take a cookie from <nick>."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan())->cookie(msg, -1);}catch(...){}
		}
	});

	bot.add_monitor(*this);
	return true;
}

// INTERFACE: IrcBotPlugin

std::string ChanopsIrcBotPlugin::get_id() const { return ID; }
std::string ChanopsIrcBotPlugin::get_name() const { return NAME; }
std::string ChanopsIrcBotPlugin::get_version() const { return VERSION; }

void ChanopsIrcBotPlugin::exit()
{
	for(auto&& inst: insts)
		inst.second->exit();
}

// INTERFACE: IrcBotMonitor

void ChanopsIrcBotPlugin::event(const message& msg)
{
	// gather intel
	// TODO: Select on message type? More efficient?

	static const str_set INFO_COMMANDS
	{
		PRIVMSG, JOIN, PART, MODE
	};

	if(INFO_COMMANDS.count(msg.command))
	{
		str nick = msg.get_nick();
		str user = msg.get_user();
		str host = msg.get_host();

		if(!nick.empty() && !user.empty() && !host.empty())
		{
			lock_guard lock(this->ircusers_mtx);

			auto found = ircusers.find({nick, user, host, 0});

			ircuser iu;
			if(found != ircusers.end())
				iu = *found;

			iu.nick = nick;
			iu.user = user;
			iu.host = host;
			iu.when = msg.when;

			ircusers.erase(iu);
			ircusers.insert(iu);

			// every 10 minutes update a database
			if(st_clk::now() > ircusers_update)
			{
				ircuser_vec iudb;

				if(auto ifs = std::ifstream(bot.getf(IRCUSER_FILE_KEY, IRCUSER_FILE_VAL)))
				{
					str line;
					while(sgl(ifs, line))
						if(siss(line) >> iu)
							iudb.push_back(iu);
					ifs.close();

					for(const auto& iu: ircusers)
						iudb.push_back(iu);

					std::sort(iudb.begin(), iudb.end(), ircuser_nickuserhost_lt());

					iudb.erase(std::unique(iudb.begin(), iudb.end(), ircuser_nickuserhost_lt()), iudb.end());

					if(auto ofs = std::ofstream(bot.getf(IRCUSER_FILE_KEY, IRCUSER_FILE_VAL)))
					{
						auto sep = "";
						for(const ircuser& iu: iudb)
							{ ofs << sep << (sss() << iu).str(); sep = "\n"; }
					}
				}

				ircusers_update = st_clk::now() + std::chrono::minutes(10);
			}
		}
	}

	auto chan = msg.get_chan();

	auto found = insts.find(chan);

	if(found != insts.end())
	{
		found->second->event(msg);
		return;
	}

	// non channel specific
	log("Unknown channel");
	BUG_COMMAND(msg);
}

}} // sookee::chanops

