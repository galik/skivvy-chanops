#ifndef SOOKEE_IRCBOT_CHANOPS_H
#define SOOKEE_IRCBOT_CHANOPS_H
/*
 * ircbot-chanops.h
 *
 *  Created on: 02 Aug 2011
 *      Author: oaskivvy@gmail.com
 */

/*-----------------------------------------------------------------.
| Copyright (C) 2016 Galik galik.bool@gmail.com                    |
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

#include <memory>
#include <map>

#include <sookee/types.h>
#include <skivvy/ircbot.h>
#include <skivvy/store.h>

namespace skivvy { namespace ircbot {

using namespace sookee::types;
using namespace skivvy::utils;

class ChanopsIrcBotPlugin;

class ChanopsApi
{
public:
	using UPtr = std::unique_ptr<ChanopsApi>;

	virtual ~ChanopsApi() {}

	virtual bool init() = 0;
	virtual void exit() = 0;
	virtual void cookie(const message& msg, int num) = 0;

	// Delegated Plugin API
	virtual void event(const message& msg) = 0;
};

class ChanopsChannel
: public ChanopsApi
{
private:
	IrcBot& bot;
	ChanopsIrcBotPlugin& plugin;
	str channel;

	Store::UPtr store;

	/**
	 * Is the bot in the channel this object
	 * monitors?
	 * @return
	 */
	bool in_channel()
	{
		return bot.chans.count(channel);
	}

public:
	ChanopsChannel(IrcBot& bot, ChanopsIrcBotPlugin& plugin, const str& channel);

	// ChanopsApi

	bool init() override;
	void exit() override;
	void cookie(const message& msg, int num) override;

	// Delegated Plugin API
	void event(const message& msg) override;
};

// ---------------------------------------------
// IRC user info
// ---------------------------------------------
struct ircuser
{
	str nick;
	str user;
	str host;
//	str flags;
	std::time_t when; // last seen

	friend sss& operator<<(sss& ss, const ircuser& iu)
	{
		ss << iu.when << ' ' << iu.host << ' ' << iu.user << ' ' << iu.nick;
		return ss;
	}

	friend sss& operator<<(sss&& ss, const ircuser& iu)
	{
		return ss << iu;
	}

	friend siss& operator>>(siss& ss, ircuser& iu)
	{
		ss >> iu.when >> iu.host >> iu.user >> iu.nick;
		return ss;
	}

	friend siss& operator>>(siss&& ss, ircuser& iu)
	{
		return ss >> iu;
	}
};

struct ircuser_userhost_lt
{ bool operator()(const ircuser& iu1, const ircuser& iu2) const
		{ return iu1.user + iu1.host < iu2.user + iu2.host; }; };

struct ircuser_nickuserhost_lt
{ bool operator()(const ircuser& iu1, const ircuser& iu2) const
	{ return iu1.nick + iu1.user + iu1.host == iu2.nick + iu2.user + iu2.host; }; };

using ircuser_set = std::set<ircuser, ircuser_userhost_lt>;
using ircuser_vec = std::vector<ircuser>;

// ---------------------------------------------

class ChanopsIrcBotPlugin final
: public BasicIrcBotPlugin
, public IrcBotMonitor
{
	std::map<str, ChanopsApi::UPtr> insts;

	// intel
	std::mutex ircusers_mtx;
	ircuser_set ircusers;
	st_time_point ircusers_update = st_clk::now();

public:
	ChanopsIrcBotPlugin(IrcBot& bot);

	// Plugin API
	str_vec api(unsigned call, const str_vec& args = {}) override;

	// INTERFACE: BasicIrcBotPlugin

	bool initialize() override;

	// INTERFACE: IrcBotPlugin

	str get_id() const override;
	str get_name() const override;
	str get_version() const override;

	void exit() override;

	// INTERFACE: IrcBotMonitor

	void event(const message& msg) override;
};

}} // skivvy::ircbot

#endif // SOOKEE_IRCBOT_CHANOPS_H
