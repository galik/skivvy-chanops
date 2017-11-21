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

#include "include/skivvy/plugin-chanops.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

#include <skivvy/logrep.h>
#include <skivvy/irc-constants.h>
#include <skivvy/irc.h>
#include <skivvy/utils.h>

#include <hol/string_utils.h>
#include <hol/simple_logger.h>

namespace skivvy {
namespace ircbot {
namespace chanops {

IRC_BOT_PLUGIN(ChanopsIrcBotPlugin);
PLUGIN_INFO("chanops", "Channel Operations", "2.0-alpha");

namespace hol {
	using namespace header_only_library::string_utils;
}

using namespace skivvy;
//using namespace sookee;
using namespace skivvy::irc;
//using namespace sookee::types;
using namespace skivvy::utils;
//using namespace sookee::utils;

using namespace header_only_library::simple_logger;

const str STORE_FILE_KEY = "chanops.store.file";
const str STORE_FILE_VAL = "chanops-store.txt"; // default

const str CHANOPS_CHANNEL_KEY = "chanops.channel";

const str IRCUSER_FILE_KEY = "chanops.ircuser.file";
const str IRCUSER_FILE_VAL = "chanops-ircuser-db.txt"; // default

const str WHOIS_RQ_DELAY_KEY = "chanops.whois.rq.delay.ms";
const siz WHOIS_RQ_DELAY_VAL = 3000; // milliseconds

sis& operator>>(sis& is, std::chrono::hours& t)
{
	siz n;
	if(is >> n)
		t = std::chrono::hours(n);
	return is;
}

sis& operator>>(sis& is, std::chrono::seconds& t)
{
	siz n;
	if(is >> n)
		t = std::chrono::seconds(n);
	return is;
}

sis& operator>>(sis& is, std::chrono::milliseconds& t)
{
	siz n;
	if(is >> n)
		t = std::chrono::milliseconds(n);
	return is;
}

//====================================================
// ChanopsChannel
//====================================================

ChanopsChannel::ChanopsChannel(IrcBot& bot, ChanopsIrcBotPlugin& plugin, const str& channel)
: bot(bot), plugin(plugin), channel(channel)
{
}

str channel_to_filename(str channel)
{
	return hol::replace_all_mute(hol::replace_all_mute(channel, "#", "H-"), "@", "A-");
}

bool ChanopsChannel::init()
{
	auto cfg_dir = bot.get_data_folder() + '/' + channel_to_filename(channel);

	std::error_code ec;
	fs::create_directories(cfg_dir, ec);
	if(ec)
	{
		LOG::E << ec.message();
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

	// initialize one ChanopsChannel per channel
	for(auto const& chan: bot.get_vec(CHANOPS_CHANNEL_KEY))
	{
		auto ret = insts.emplace(std::piecewise_construct,
			std::forward_as_tuple(chan),
			std::forward_as_tuple(bot, *this, chan));

		if(!ret.second)
		{
			LOG::E << "config has duplicate Chanops for channel: " << chan;
			continue;
		}

		if(!ret.first->second.init())
		{
			LOG::E << "E: initializing Chanops for channel: " << chan;
			insts.erase(ret.first);
			continue;
		}
	}

	add
	({
		"!cookies"
		, "!cookies <nick> Show <nick>'s cookies."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan()).cookie(msg, 0);}catch(...){}
		}
	});
	add
	({
		"!cookie++"
		, "!cookie++ <nick> Give <nick> a cookie."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan()).cookie(msg, 1);}catch(...){}
		}
	});
	add
	({
		"!cookie--"
		, "!cookie-- <nick> Take a cookie from <nick>."
		, [this](const message& msg)
		{
			try{insts.at(msg.get_chan()).cookie(msg, -1);}catch(...){}
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
		inst.second.exit();
}

void ChanopsIrcBotPlugin::insert_ircuser(const str& nick, const str& user, const str& host, std::time_t when)
{
	lock_guard lock(this->ircusers_mtx);

	auto found = ircusers.find({nick, user, host, 0});

	ircuser iu;
	if(found != ircusers.end())
		iu = *found;

	iu.nick = nick;
	iu.user = user;
	iu.host = host;
	iu.when = when;

	ircusers.erase(iu);
	ircusers.insert(iu);
}

void ChanopsIrcBotPlugin::save_ircusers()
{
	lock_guard lock(this->ircusers_mtx);

	// every 10 minutes update a database
	if(st_clk::now() > ircusers_update)
	{
		ircuser iu;
		ircuser_vec iudb;

		if(auto ifs = std::ifstream(bot.getf(IRCUSER_FILE_KEY, IRCUSER_FILE_VAL)))
		{
			bug("LOADING: iudb");
			str line;
			while(sgl(ifs, line))
				if(siss(line) >> iu)
					iudb.push_back(iu);
			ifs.close();

			bug_cnt(iudb);

			for(const auto& iu: ircusers)
				iudb.push_back(iu);

			bug_cnt(iudb);

			std::sort(iudb.begin(), iudb.end(), ircuser_host_user_nick_lt_when_gt());

			bug_cnt(iudb);

			iudb.erase(std::unique(iudb.begin(), iudb.end(), ircuser_host_user_nick_eq()), iudb.end());

			bug_cnt(iudb);

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

// INTERFACE: IrcBotMonitor

void ChanopsIrcBotPlugin::event(const message& msg)
{
	if(msg.command == PING || msg.command == PONG)
		return;

	BUG_COMMAND(msg);

	//======================//
	// DEBUG INFO GATHERING //
	static str_set info_cmds;
	if(!info_cmds.count(msg.command)
	&& !msg.get_nick().empty()
	&& !msg.get_user().empty()
	&& !msg.get_host().empty())
	{
		info_cmds.insert(msg.command);
		LOG::D << "INFO GATHERING: " << msg.command << " may contain ircuser info";
	}
	//======================//

	auto chan = msg.get_chan();

	//--------------------------------------------------------
	// Collect passive IRC user information in order to
	// identify users as uniquely as possible
	//--------------------------------------------------------
	//
	static const str_set INFO_COMMANDS
	{
		// NOTICE - only when its the bot or the server
		  PRIVMSG, JOIN, PART, MODE, QUIT
		, RPL_WHOISUSER
	};

	if(INFO_COMMANDS.count(msg.command))
	{
		//bug("INFO_COMMAND: " << msg.command);
		str nick;
		str user;
		str host;

		if(msg.command == RPL_WHOISUSER)
		{
			//bug("INFO_COMMAND: RPL_WHOISUSER detected");
			auto params = msg.get_params();

			//bug_cnt(params);

			if(params.size() < 4)
				LOG::E << "expected 4 parameters for: " << msg.command << ", got: " << params.size();
			else
			{
				nick = params[1];
				user = params[2];
				host = params[3];
			}
		}
		else
		{
			nick = msg.get_nick();
			user = msg.get_user();
			host = msg.get_host();
		}

		LOG::D << "gathering passive info: " << nick << " " << user << " " << host;

		bug_var(nick);
		bug_var(user);
		bug_var(host);

		if(!nick.empty() && !user.empty() && !host.empty())
		{
			insert_ircuser(nick, user, host, msg.when);
			save_ircusers();
		}
	}
	//
	//--------------------------------------------------------

	//--------------------------------------------------------
	// Actively solicit information when joining a channel
	//--------------------------------------------------------
	//
	// When joining a channel nicks are sent to the joiner
	// by means of RPL_NAMREPLY
	//
	if(msg.command == RPL_NAMREPLY)
	{
		str nick;
		str_set whoiss;
		siss iss(msg.get_trailing());

		while(iss >> nick)
		{
			LOG::D << "soliciting info for: " << chan << " " << nick;
			if(nick.empty())
				continue;

			if(nick[0] == '+' || nick[0] == '@')
				nick.erase(0, 1);

			// TODO: Do I need to sent my nick to discover
			// if I have ops or not?
			if(nick != bot.nick)
				whoiss.insert(nick);
		}

		// try these one by one bcus not working
		// in batch mode on freenode for some reason

		const auto delay = std::chrono::milliseconds(bot.get(WHOIS_RQ_DELAY_KEY, WHOIS_RQ_DELAY_VAL));

		if(!whoiss.empty()) std::thread([this,delay,whoiss = std::move(whoiss)]
		{
			for(auto& nick: whoiss)
			{
				irc->whois({nick});
				std::this_thread::sleep_for(delay);
			}
		}).detach();

		// WHOIS
		// RPL_WHOISUSER     :quakenet.org 311 Skivvy SooKee ~SooKee SooKee.users.quakenet.org * :SooKee
		// RPL_WHOISCHANNELS :quakenet.org 319 Skivvy SooKee :@#skivvy @#openarenahelp +#openarena @#omfg
		// RPL_WHOISSERVER   :quakenet.org 312 Skivvy SooKee *.quakenet.org :QuakeNet IRC Server
		// UD                :quakenet.org 330 Skivvy SooKee SooKee :is authed as
		// RPL_ENDOFWHOIS    :quakenet.org 318 Skivvy SooKee :End of /WHOIS list.
	}
	//
	//--------------------------------------------------------

	//--------------------------------------------------------
	// Dispatch channel events to the relevant channel
	// monitor
	//--------------------------------------------------------
	//
	auto found = insts.find(chan);

	if(found != insts.end())
	{
		found->second.event(msg);
		return;
	}
	//
	//--------------------------------------------------------
}

} // chanops
} // ircbot
} // skivvy
