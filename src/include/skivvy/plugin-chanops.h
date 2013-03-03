#pragma once
#ifndef _SOOKEE_IRCBOT_CHANOPS_H_
#define _SOOKEE_IRCBOT_CHANOPS_H_
/*
 * ircbot-chanops.h
 *
 *  Created on: 02 Aug 2011
 *      Author: oaskivvy@gmail.com
 */

/*-----------------------------------------------------------------.
| Copyright (C) 2011 SooKee oaskivvy@gmail.com               |
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

#include <skivvy/ircbot.h>

#include <bitset>
#include <mutex>
#include <map>

#include <skivvy/store.h>
#include <skivvy/mail.h>

namespace skivvy { namespace ircbot {

using namespace skivvy::email;
using namespace skivvy::utils;

#define CHANOPS_GROUP(name) \
static const str G_##name = #name

// command permits
CHANOPS_GROUP(NONE);
CHANOPS_GROUP(USER);
CHANOPS_GROUP(OPER);
CHANOPS_GROUP(SUPR);
CHANOPS_GROUP(ROOT);

static const str G_ANY = "*";

CHANOPS_GROUP(CUST); // custom group prefix ?

// action requirements
CHANOPS_GROUP(VOICED);
CHANOPS_GROUP(OPPED);
CHANOPS_GROUP(BANNED);

struct ircuser
{
	str nick;
	str user;
	str host;

	bool operator<(const ircuser& u) const { return user + host < u.user + u.host; }
	bool operator==(const ircuser& u) const { return nick + user + host == u.nick + u.user + u.host; }

	friend sss& operator<<(sss& ss, const ircuser& u)
	{
		ss << u.host << ' ' << u.user << ' ' << u.nick;
		return ss;
	}

	friend sss& operator<<(sss&& ss, const ircuser& u)
	{
		return ss << u;
	}

	friend siss& operator>>(siss& ss, ircuser& u)
	{
		ss >> u.host >> u.user >> u.nick;
		return ss;
	}

	friend siss& operator>>(siss&& ss, ircuser& u)
	{
		return ss >> u;
	}
};

typedef std::set<ircuser> ircuser_set;
typedef ircuser_set::iterator ircuser_set_iter;
typedef std::pair<ircuser_set_iter, bool> ircuser_set_pair;
typedef std::vector<ircuser> ircuser_vec;
typedef ircuser_vec::iterator ircuser_vec_iter;

ircuser_set_iter find_by_nick(const ircuser_set& s, const str& nick)
{
	return std::find_if(s.begin(), s.end(), [=](const ircuser& u){ return nick == u.nick; });
}

ircuser_set_iter find_by_user(const ircuser_set& s, const str& user)
{
	return std::find_if(s.begin(), s.end(), [=](const ircuser& u){ return user == u.user; });
}

bool found_by_nick(const ircuser_set& s, const str& nick)
{
	return std::find_if(s.begin(), s.end(), [=](const ircuser& u){ return nick == u.nick; }) != s.end();
}

bool found_by_user(const ircuser_set& s, const str& user)
{
	return std::find_if(s.begin(), s.end(), [=](const ircuser& u){ return user == u.user; }) != s.end();
}

class ChanopsIrcBotPlugin _final_
: public BasicIrcBotPlugin
, public IrcBotMonitor
{
private:

	SMTP smtp;

	std::mutex store_mtx;

	std::mutex ircusers_mtx;
	ircuser_set ircusers; // nick -> userhost
	st_time_point ircuser_update;

	// tack back server stuff
	str_set tb_ops;

	std::mutex chanops_mtx;
	str_siz_map chanops; // chan -> bool (have ops in that channel)


public:

	/*
	 * User database record
	 */
	struct user_r
	{
		str user; // the user by which we log in as
		uint32_t sum; // password sum
		str_set groups;
		str email;

		// <user>:<sum>:group1,group2

		friend std::istream& operator>>(std::istream& is, user_r& ur);
		friend std::ostream& operator<<(std::ostream& os, const user_r& ur);
	};


	/**
	 * User object
	 */
	struct user_t
	{
//		str prefix; // <nick>!<ircuser>@<host>
		std::time_t login_time;
		str userhost; // <ircuser>@<host>
		str user; // the user by which we logged in as
		str nick; // current nick
		str_set groups;

		user_t():login_time(0) {}
		user_t(const message& msg, const user_r& ur)
		: userhost(msg.get_userhost()), user(ur.user)
		, nick(msg.get_nick()), groups(ur.groups)
		{}

		bool operator<(const user_t& u) const { return user < u.user; }
		bool operator==(const user_t& u) const { return user == u.user; }

		friend std::istream& operator>>(std::istream& is, user_t& u);
		friend std::ostream& operator<<(std::ostream& os, const user_t& u);
	};

	typedef std::set<user_t> user_set;
	typedef user_set::iterator user_iter;
	typedef user_set::const_iterator user_citer;

private:

	BackupStore store;

	std::mutex users_mtx;
	user_set users;
	std::mutex bans_mtx;
	str_set bans;

//	bool extract_params(const message& msg, std::initializer_list<str*> args);

	/**
	 * Verify of the user sending the message has
	 * the various permissions.
	 */
	bool permit(const message& msg);

	bool signup(const message& msg);
	bool email_signup(const message& msg);

	std::mutex vote_mtx;
	std::map<str, bool> vote_in_progress;
//	str_map vote_nick;
	std::map<str, std::future<void>> vote_fut;
	str_siz_map vote_f1;
	str_siz_map vote_f2;
	str_set_map voted; // who already voted
	bool vote_track_nick = false;

	bool votekick(const message& msg);
	bool f1(const message& msg);
	bool f2(const message& msg);
	bool ballot(const str& chan, const str& user, const str& oldnick, const st_time_point& end);
	bool cookie(const message& msg, int num);

	bool login(const message& msg);
	//void apply_acts(const str& id);
	void apply_acts(const user_t& u);


	/**
	 * Rules found in the config file
	 */
	bool enforce_static_rules(const str& chan, const str& prefix, const str& nick);

	/**
	 * Rules found in the persistant store
	 */
	bool enforce_dynamic_rules(const str& chan, const str& prefix, const str& nick);
	bool kickban(const str& chan, const str& nick);

	/**
	 * List users
	 */
	bool list_users(const message& msg);
	bool ban(const message& msg);
	bool banlist(const message& msg);
	bool unban(const message& msg);
	bool seen(const message& msg);
	bool tell(const message& msg);
	bool talk_event(const message& msg);
	bool name_event(const message& msg);
	bool join_event(const message& msg);
	bool kick_event(const message& msg);
	bool mode_event(const message& msg);
	bool nick_event(const message& msg);
	bool whoisuser_event(const message& msg);

//	RandomTimer rt;

	bool reclaim(const message& msg);

	// Assign each function to a group
	str_map perms; // msg.get_user_cmd() -> G_GROUP

//	void op(str& nick);
//	void voice(str& nick);
//	void kick(str& nick);
//	void ban(str& nick);

public:
	ChanopsIrcBotPlugin(IrcBot& bot);
	virtual ~ChanopsIrcBotPlugin();

	// Plugin API
	bool is_userhost_logged_in(const str& userhost);
	str get_userhost_username(const str& userhost);

	enum class status
	{
		OK
		, FAIL
		, GROUP_ALREADY_EXISTS
	};

	status create_custom_group(const str& group);
	bool add_user_to_custom_group(const str& user, const str& group);

	// INTERFACE: BasicIrcBotPlugin

	virtual bool initialize() _override_;

	// INTERFACE: IrcBotPlugin

	virtual str get_id() const _override_;
	virtual str get_name() const _override_;
	virtual str get_version() const _override_;
	virtual void exit() _override_;

	// INTERFACE: IrcBotMonitor

	virtual void event(const message& msg) _override_;
};

}} // sookee::ircbot

#endif // _SOOKEE_IRCBOT_CHANOPS_H_
