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

#include <skivvy/logrep.h>
#include <skivvy/stl.h>
#include <skivvy/str.h>
#include <skivvy/ios.h>
#include <skivvy/irc-constants.h>
#include <skivvy/irc.h>

namespace skivvy { namespace ircbot {

IRC_BOT_PLUGIN(ChanopsIrcBotPlugin);
PLUGIN_INFO("chanops", "Channel Operations", "0.1");

using namespace skivvy;
using namespace skivvy::irc;
using namespace skivvy::types;
using namespace skivvy::utils;
using namespace skivvy::string;

const str STORE_FILE = "chanops.store.file";
const str STORE_FILE_DEFAULT = "chanops-store.txt";

static const str BAN_FILE = "chanops.ban.file";
static const str BAN_FILE_DEFAULT = "chanops-bans.txt";

static const str USER_FILE = "chanops.user.file";
static const str USER_FILE_DEFAULT = "chanops-users.txt";

const str GREET_JOINERS_VEC = "chanops.greet.active";

const str UNGREET_FILE = "chanops.ungreet.file";
const str UNGREET_FILE_DEFAULT = "chanops-ungreets.txt";

const str GREETINGS_VEC = "chanops.greet";
const str GREET_MIN_DELAY = "chanops.greet.delay.min";
const siz GREET_MIN_DELAY_DEFAULT = 1;
const str GREET_MAX_DELAY = "chanops.greet.delay.max";
const siz GREET_MAX_DELAY_DEFAULT = 6;

const str CHANOPS_TAKEOVER_DEOP = "chanops.takeover.deop";
const str CHANOPS_TAKEOVER_BAN = "chanops.takeover.ban";
const str CHANOPS_TAKEOVER_KEY = "chanops.takeover.key";

const str TAKEOVER_KICK_MSG;
const str TAKEOVER_KICK_MSG_DEFAULT = "Reclaiming channel.";

const str CHANOPS_WILD_OP_VEC = "chanops.wild.op";
const str CHANOPS_PCRE_OP_VEC = "chanops.pcre.op";

const str CHANOPS_WILD_PROTECT_VEC = "chanops.wild.protect";
const str CHANOPS_PCRE_PROTECT_VEC = "chanops.pcre.protect";

const str CHANOPS_WILD_KICK_VEC = "chanops.wild.kick";
const str CHANOPS_PCRE_KICK_VEC = "chanops.pcre.kick";
const str CHANOPS_WILD_BAN_VEC = "chanops.wild.ban";
const str CHANOPS_PCRE_BAN_VEC = "chanops.pcre.ban";

const str CHANOPS_WILD_VOICE_VEC = "chanops.wild.voice";
const str CHANOPS_PCRE_VOICE_VEC = "chanops.pcre.voice";

const str CHANOPS_WILD_MODE_VEC = "chanops.wild.mode";
const str CHANOPS_PCRE_MODE_VEC = "chanops.pcre.mode";

static uint32_t checksum(const std::string& pass)
{
	if(pass.size() < sizeof(uint32_t))
		return 0;
	uint32_t sum = 0xAA55AA55; // salt
	for(size_t i = 0; i < pass.size() - sizeof(uint32_t); ++i)
		sum *= *reinterpret_cast<const uint32_t*>(&pass[i]);
	return sum;
}


std::ostream& operator<<(std::ostream& os, const ChanopsIrcBotPlugin::user_r& ur)
{
	bug_func();

	// <user>:<sum>:group1,group2

	os << ur.user <<  ':' << std::hex << ur.sum << ':' << ur.email << ':';
	str sep;
	for(const str& g: ur.groups)
		{ os << sep << g; sep = ","; }

	return os;
}
std::istream& operator>>(std::istream& is, ChanopsIrcBotPlugin::user_r& ur)
{
	bug_func();

	// <user>:<sum>:group1,group2

	sgl(is, ur.user, ':');
	(is >> std::hex >> ur.sum).ignore();
	sgl(is, ur.email, ':');
	str group;
	while(std::getline(is, group, ','))
		ur.groups.insert(group);
	return is;
}

ChanopsIrcBotPlugin::ChanopsIrcBotPlugin(IrcBot& bot)
: BasicIrcBotPlugin(bot)
, store(bot.getf(STORE_FILE, STORE_FILE_DEFAULT))
{
//	smtp.mailfrom = "<noreply@sookee.dyndns.org>";
	smtp.mailfrom = "<noreply@cpc2-pool13-2-0-cust799.15-1.cable.virginmedia.com>";
}

ChanopsIrcBotPlugin::~ChanopsIrcBotPlugin() {}

bool ChanopsIrcBotPlugin::permit(const message& msg)
{
	BUG_COMMAND(msg);

	const str& group = perms[msg.get_user_cmd()];

	// not accessible via exec(msg);
	if(group.empty())
		return false;

	// anyone can call
	if(group == "*")
		return true;

	bool in = false;
	for(const user_t& u: users)
	{
		if(u.userhost != msg.get_userhost())
			continue;
		in = true;
		for(const str& g: u.groups)
			if(g == group)
				return true;
		break;
	}

	if(in)
		bot.fc_reply_pm(msg, "ERROR: You do not have sufficient access.");
	else
		bot.fc_reply_pm(msg, "You are not logged in.");

	return false;
}

bool ChanopsIrcBotPlugin::signup(const message& msg)
{
	BUG_COMMAND(msg);

	std::string user, pass, pass2;
	if(!bot.extract_params(msg, {&user, &pass, &pass2}))
		return false;

	bug("user: " << user);
	bug("pass: " << pass);
	bug("pass2: " << pass2);

	if(pass.size() < sizeof(uint32_t))
		return bot.cmd_error_pm(msg, "ERROR: Password must be at least 4 characters long.");

	if(pass != pass2)
		return bot.cmd_error_pm(msg, "ERROR: Passwords don't match.");

	if(store.has("user." + user))
		return bot.cmd_error_pm(msg, "ERROR: User already registered.");

	user_r ur;
	ur.user = user;
	ur.groups.insert(G_USER);
	ur.sum = checksum(pass);

	store.set("user." + user, ur);

	bot.fc_reply_pm(msg, "Successfully registered.");
	return true;
}

str gen_password(siz len = 8)
{
	str pass;
	for(siz n, i = 0; i < len; ++i)
		pass += (n = rand_int(0, 61)) < 26 ? ('a' + n) : n < 52 ? ('A' + n - 26) : ('0' + n - 52);
	return pass;
}

str gen_boundary(siz len = 64)
{
	str bound;
	for(siz n, i = 0; i < len; ++i)
		bound += (n = rand_int(0, 15)) < 10 ? ('0' + n) : ('A' + n - 10);
	return bound;
}

bool ChanopsIrcBotPlugin::email_signup(const message& msg)
{
	BUG_COMMAND(msg);

	std::string user, email, email2;
	if(!bot.extract_params(msg, {&user, &email, &email2}))
		return false;

	bug("user  : " << user);
	bug("email : " << email);
	bug("email2: " << email2);

	if(!bot.preg_match("\\w+@[^.]+\\.[\\w.]+", email, true))
		return bot.cmd_error_pm(msg, "ERROR: Invalid email address: " + email);

	if(email != email2)
		return bot.cmd_error_pm(msg, "ERROR: Email addresses don't match.");

	if(store.has("user." + user))
		return bot.cmd_error_pm(msg, "ERROR: User already registered.");

	str pass = gen_password();

	user_r ur;
	siz retries = 4;

	sifs ifs(bot.getf("chanops.email.template", "chanops-email-template.txt"));
	if(!ifs)
	{
		log("Unable to open email template: " << bot.getf("chanops.email.template", "chanops-email-template.txt"));
		return bot.cmd_error(msg, "Failed to send email - signup aborted, please try again later.");
	}

	char c;
	str body;
	while(ifs.get(c))
		body += c;

	str boundary = gen_boundary();

	replace(body, "$SUBJECT", "Skivvy's Email Sighnup");
	replace(body, "$FROM", "<oaskivvy@gmail.com>");
	replace(body, "$TO", "<" + email + ">");
	replace(body, "$USER", user);
	replace(body, "$PASS", pass);
	replace(body, "$BOTNAME", bot.nick);
	replace(body, "$BOUNDARY", boundary);

	smtp.rcptto = "<" + email + ">";
	smtp.to = "<" + email + ">";
	while(--retries && !smtp.sendmail(bot.nick + ", chanops signup", body));

	if(!retries)
		return bot.cmd_error(msg, "Failed to send email - signup aborted, please try again later.");

	ur.user = user;
	ur.groups.insert(G_USER);
	ur.sum = checksum(pass);
	ur.email = email;

	store.set("user." + user, ur);

	bot.fc_reply_pm(msg, "Successfully registered.");
	return true;
}

bool ChanopsIrcBotPlugin::login(const message& msg)
{
	BUG_COMMAND(msg);

	std::string user;
	std::string pass;
	if(!bot.extract_params(msg, {&user, &pass}))
		return false;

	bug("user: " << user);
	bug("pass: " << pass);
	uint32_t sum = checksum(pass);
	bug("sum: " << std::hex << sum);

	lock_guard lock(users_mtx);

	str user_key = "user." + user;

	if(!store.has(user_key))
		return bot.cmd_error_pm(msg, "ERROR: Username not found.");

	user_r ur;
	ur = store.get(user_key, ur);

	if(ur.groups.count(G_BANNED))
		return bot.cmd_error_pm(msg, "ERROR: Banned");

	if(ur.sum != sum)
		return bot.cmd_error_pm(msg, "ERROR: Bad password");

	user_t u(msg, ur);

	if(users.count(u))
		return bot.cmd_error_pm(msg, "You are already logged in to " + bot.nick);

	users.insert(u);

	str sep;
	soss oss;
	for(const str& group: u.groups)
	{
		oss << sep << group;
		sep = ", ";
	}
	bot.fc_reply_pm(msg, "You are now logged in to " + bot.nick + ": " + oss.str());

	apply_acts(u);

	return true;
}

bool ChanopsIrcBotPlugin::is_userhost_logged_in(const str& userhost)
{
	for(const user_t& u: users)
		if(u.userhost == userhost)
			return true;
	return false;
}

str ChanopsIrcBotPlugin::get_userhost_username(const str& userhost)
{
	for(const user_t& u: users)
		if(u.userhost == userhost)
			return u.user;
	return "";
}

ChanopsIrcBotPlugin::status ChanopsIrcBotPlugin::create_custom_group(const str& group)
{
	// Ensure group is unique
	for(const str& g: store.get_vec("custom-group"))
		if(g == group)
			return status::GROUP_ALREADY_EXISTS;
	store.add("custom-group", group);
	return status::OK;
}

bool ChanopsIrcBotPlugin::add_user_to_custom_group(const str& user, const str& group)
{
	return true;
}


void ChanopsIrcBotPlugin::apply_acts(const user_t& /*u*/)
{
//	if(u.groups.count(G_BANNED))
//		irc->mode(u.nick, "+b");
//	else if(u.groups.count(G_OPPED))
//		irc->mode(u.nick, "+o");
//	else if(u.groups.count(G_VOICED))
//		irc->mode(u.nick, "+v");
}

bool ChanopsIrcBotPlugin::list_users(const message& msg)
{
	BUG_COMMAND(msg);

	if(!permit(msg))
		return false;

	bot.fc_reply_pm(msg, "Logged in users:");

	lock_guard lock(users_mtx);

	for(const user_t& u: users)
		bot.fc_reply_pm(msg, u.user + ": " + u.userhost);
	return true;
}

bool ChanopsIrcBotPlugin::kickban(const str& chan, const str& nick)
{
	return irc->mode(chan, " +b *!*" + nick + "@*") && irc->kick({chan}, {nick}, "Bye bye.");
}

bool ChanopsIrcBotPlugin::ban(const message& msg)
{
	BUG_COMMAND(msg);

	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error_pm(msg, "ERROR: !ban can only be used from a channel.");

	str chan = msg.to;

	// <nick>|<pcre>
	if(bot.nicks[chan].count(msg.get_nick())) // ban by nick
	{
		store.set("ban.nick." + chan, msg.get_nick());
		enforce_rules(msg.to, msg.get_nick());
	}
	else // ban by regex on userhost
	{
		store.set("ban.pcre." + chan, msg.get_user_params());
		enforce_rules(msg.to, msg.get_nick());
	}

	return true;
}

bool ChanopsIrcBotPlugin::reclaim(const message& msg)
{
	BUG_COMMAND(msg);
	if(!permit(msg))
		return false;

	// cmd: !reclaim
	//                  from: SooKee!~SooKee@SooKee.users.quakenet.org
	//                   cmd: PRIVMSG
	//                params: #skivvy
	//                    to: #skivvy
	//                  text: !reclaim SooKee
	// msg.from_channel()   : true
	// msg.get_nick()       : SooKee
	// msg.get_user()       : ~SooKee
	// msg.get_host()       : SooKee.users.quakenet.org
	// msg.get_userhost()   : ~SooKee@SooKee.users.quakenet.org
	// msg.get_user_cmd()   : !reclaim
	// msg.get_user_params(): SooKee
	// msg.reply_to()       : #skivvy
	// 2013-01-16 09:12:51: IrcBot::execute(): Unknown command: !reclaim

//	users;

	str nick;
	if(!bot.extract_params(msg, {&nick}))
		return false;

	bug("nick: " << nick);

	return true;
}

str col(const str& fg, const str& bg)
{
	return IRC_COLOR + fg + "," + bg;
}

str red = IRC_COLOR + IRC_Red + "," + IRC_White;
str black = IRC_COLOR + IRC_Black + "," + IRC_White;
str blue = IRC_COLOR + IRC_Navy_Blue + "," + IRC_White;
str green = IRC_COLOR + IRC_Green + "," + IRC_White;
str bold = IRC_BOLD;

bool ChanopsIrcBotPlugin::votekick(const message& msg)
{
	BUG_COMMAND(msg);
	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error(msg, "You can only call a vote from a channel.");

	str chan = msg.to;

	lock_guard lock(vote_mtx);

	if(vote_in_progress[chan])
		return bot.cmd_error(msg, "Vote already in progress.", true);

	str nick;
	str reason;
	siss iss(msg.get_user_params());
	if(!(sgl(iss >> nick >> std::ws, reason)))
		return bot.cmd_error(msg, "usege: !votekick <nick> <reason> *(<duration>)");

	siz secs = 60;

	bot.fc_reply(msg, bold + red + "VOTE-KICK: " + black + "Vote" + blue +
		+ " if you think we should kick " + black + nick + blue + " from this channel!");
	bot.fc_reply(msg, bold + red + "VOTE-KICK: " + blue
		+ "Reason: " + red + reason + black + " !f1 = YES, !f2 = NO");
	bot.fc_reply(msg, bold + red + "VOTE-KICK: " + blue
		+ "You have " + black + std::to_string(secs) + blue + " seconds to comply.");

	vote_f1[chan] = 0;
	vote_f2[chan] = 0;
	voted[chan].clear();

	vote_in_progress[chan] = true;
	vote_fut[chan] = std::async(std::launch::async, [=]{ ballot(chan, nick, st_clk::now() + std::chrono::seconds(secs)); });

	return true;
}

bool ChanopsIrcBotPlugin::f1(const message& msg)
{
	BUG_COMMAND(msg);

	if(!msg.from_channel())
		return bot.cmd_error(msg, "You can only vote from a channel.");

	str chan = msg.to;
	lock_guard lock(vote_mtx);

	if(!vote_in_progress[chan])
		return bot.cmd_error(msg, "There is no vote in progress.", true);

	if(voted[chan].count(msg.get_userhost()))
		bot.fc_reply(msg, bold + red + "VOTE-KICK: " + blue
			+ "Sorry " + black + msg.get_nick() + blue + ", you can only vote once.");
	else
	{
		++vote_f1[chan];
		voted[chan].insert(msg.get_userhost());
		log("votekick: " << msg.get_userhost() << " voted f1");
	}

	return true;
}

bool ChanopsIrcBotPlugin::f2(const message& msg)
{
	BUG_COMMAND(msg);

	if(!msg.from_channel())
		return bot.cmd_error(msg, "You can only vote from a channel.");

	str chan = msg.to;
	lock_guard lock(vote_mtx);

	if(!vote_in_progress[chan])
		return bot.cmd_error(msg, "There is no vote in progress.", true);

	if(voted[chan].count(msg.get_userhost()))
		bot.fc_reply(msg, bold + red + "VOTE-KICK: " + blue
			+ "Sorry " + black + msg.get_nick() + blue + ", you can only vote once.");
	else
	{
		++vote_f2[chan];
		voted[chan].insert(msg.get_userhost());
		log("votekick: " << msg.get_userhost() << " voted f2");
	}

	return true;
}

bool ChanopsIrcBotPlugin::ballot(const str& chan, const str& nick, const st_time_point& end)
{
	while(st_clk::now() < end)
		std::this_thread::sleep_until(end);
	lock_guard lock(vote_mtx);
	vote_in_progress[chan] = false;
	bug_var(chan);
	bug_var(vote_f1[chan]);
	bug_var(vote_f2[chan]);
	if(vote_f1[chan] > vote_f2[chan])
	{
		irc->say(chan, bold + red + "VOTE-KICK: " + black
			+ nick + blue + " : The people have spoken and it was not good "
			+ black + std::to_string(vote_f1[chan]) + " - "
			+ std::to_string(vote_f2[chan]) + blue + " to kick!"
			+ black + " :'(");
		irc->kick({chan}, {nick}, "You are the weakest link.... goodby!");
	}
	else
		irc->say(chan, bold + red + "VOTE-KICK: " + blue + "The scrawney life of "
			+ black + nick + blue + " has been saved by the people!");

	return true;
}
// every function belongs to a group

// INTERFACE: BasicIrcBotPlugin

bool ChanopsIrcBotPlugin::initialize()
{
//	std::ifstream ifs(bot.getf(BAN_FILE, BAN_FILE_DEFAULT));
	perms =
	{
		{"!users", G_USER}
		, {"!reclaim", G_USER}
		, {"!ban", G_OPER}
		, {"!votekick", G_OPER}
	};

	// chanops.init.user: <user> <pass> <PERM> *( "," <PERM> )
	for(const str& init: bot.get_vec("chanops.init.user"))
	{
		str user, pass, email, list;

		if(!sgl(siss(init) >> user >> pass >> email >> std::ws, list))
			continue;

		if(!store.has("user." + user))
		{
			user_r ur;
			ur.user = user;
			ur.sum = checksum(pass);
			ur.email = email;

			str group;
			siss iss(list);
			while(sgl(iss, group, ','))
				ur.groups.insert(trim(group));

			store.set("user." + user, ur);
		}
	}

	add
	({
		"!reclaim" // no ! indicated PM only command
		, "!reclaim <nick> - notify when <nick> is unused (must be logged in)."
		, [&](const message& msg){ reclaim(msg); }
	});
	add
	({
		"register" // no ! indicated PM only command
		, "register <username> <password> <password>"
		, [&](const message& msg){ email_signup(msg); }
	});
	add
	({
		"!votekick"
		, "!votekick <nick> <reason>"
		, [&](const message& msg){ votekick(msg); }
	});
	add
	({
		"!f1"
		, "!f1 means vote YES during !votekick"
		, [&](const message& msg){ f1(msg); }
	});
	add
	({
		"!f2"
		, "!f2 means vote NO during !votekick"
		, [&](const message& msg){ f2(msg); }
	});
	add
	({
		"login" // no ! indicated PM only command
		, "login <username> <password>"
		, [&](const message& msg){ login(msg); }
	});
	add
	({
		"!users"
		, "!users List logged in users."
		, [&](const message& msg){ list_users(msg); }
	});
	add
	({
		"!ban"
		, "!ban <nick>|<regex> - ban either a registered user OR a regex match on userhost."
		, [&](const message& msg){ ban(msg); }
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
//	bug_func();
}

// INTERFACE: IrcBotMonitor

void ChanopsIrcBotPlugin::event(const message& msg)
{
//	BUG_COMMAND(msg);
	if(msg.cmd == RPL_NAMREPLY)
		name_event(msg);
	else if(msg.cmd == NICK)
		nick_event(msg);
	else if(msg.cmd == RPL_WHOISUSER || msg.cmd == RPL_WHOISCHANNELS || msg.cmd == RPL_WHOISOPERATOR)
		whoisuser_event(msg);
	else if(msg.cmd == JOIN)
		join_event(msg);
	else if(msg.cmd == MODE)
		mode_event(msg);
	else if(msg.cmd == KICK)
		kick_event(msg);
}


bool ChanopsIrcBotPlugin::nick_event(const message& msg)
{
	BUG_COMMAND(msg);
	return true;
}

bool ChanopsIrcBotPlugin::whoisuser_event(const message& msg)
{
	BUG_COMMAND(msg);
	// -----------------------------------------------------
	//                  from: dreamhack.se.quakenet.org
	//                   cmd: 311
	//                params: Skivvy00 SooKee ~SooKee SooKee.users.quakenet.org *
	//                    to: Skivvy00
	//                  text: SooKee
	// msg.from_channel()   : false
	// msg.get_nick()       : dreamhack.se.quakenet.org
	// msg.get_user()       : dreamhack.se.quakenet.org
	// msg.get_host()       : dreamhack.se.quakenet.org
	// msg.get_userhost()   : dreamhack.se.quakenet.org
	// msg.get_user_cmd()   : SooKee
	// msg.get_user_params():
	// msg.reply_to()       : dreamhack.se.quakenet.org
	// -----------------------------------------------------

	str skip, nick, userhost;

	lock_guard lock(nicks_mtx);
	if(siss(msg.params) >> skip >> nick >> skip >> userhost)
	{
		bug_var(userhost);
		nicks[nick] = userhost;
	}

	const str tb_chan = bot.get("chanops.takover.chan");

	for(const str& op: tb_ops)
	{
		if(!nicks[op].empty())
		{
			bug_var(nicks[op]);
			str mask = "*!*@*";
			siz pos = nicks[op].rfind(".");
			bug_var(pos);
			pos = nicks[op].rfind(".", --pos);
			bug_var(pos);
			mask += nicks[op].substr(++pos);
			bug_var(mask);
			irc->mode(tb_chan, " +b " + mask);
		}
	}
	tb_ops.clear();

	return true;
}

bool ChanopsIrcBotPlugin::name_event(const message& msg)
{
	BUG_COMMAND(msg);

	// :dreamhack.se.quakenet.org 353 Skivvy = #openarena :Skivvy +SooKee +I4C @OAbot +Light3r +pet

	str skip, chan;
	siss iss(msg.params);
	sgl(iss, skip, '#') >> chan;
	chan.insert(0, "#");
	bug_var(chan);

	str tb_chan = bot.get("chanops.takover.chan");

	str nick;

	iss.clear();
	iss.str(msg.text);
	iss >> nick; // ignogre my nick

	bug_var(nick);

	lock_guard lock(nicks_mtx);
	while(iss >> nick)
	{
		if(!nick.empty())
		{
			if(chan == tb_chan && nick[0] == '@' && nick != "Q" && nick != "@" + bot.nick)
				tb_ops.insert(nick.substr(1));

			if(nick[0] == '+' || nick[0] == '@')
				nick.erase(0, 1);

			if(nicks.find(nick) == nicks.end())
			{
				// TODO: re-add this when I actually use the info.
				//irc->whois(nick); // initiate request, see whois_event() for response
				nicks[nick];
			}
		}
	}

	// WHOIS
	// RPL_WHOISUSER     :quakenet.org 311 Skivvy SooKee ~SooKee SooKee.users.quakenet.org * :SooKee
	// RPL_WHOISCHANNELS :quakenet.org 319 Skivvy SooKee :@#skivvy @#openarenahelp +#openarena @#omfg
	// RPL_WHOISSERVER   :quakenet.org 312 Skivvy SooKee *.quakenet.org :QuakeNet IRC Server
	// UD                :quakenet.org 330 Skivvy SooKee SooKee :is authed as
	// RPL_ENDOFWHOIS    :quakenet.org 318 Skivvy SooKee :End of /WHOIS list.


	return true;
}

std::future<void> fut;

bool ChanopsIrcBotPlugin::mode_event(const message& msg)
{
	BUG_COMMAND(msg);

	// -----------------------------------------------------
	//                  from: SooKee!~SooKee@SooKee.users.quakenet.org
	//                   cmd: MODE
	//                params: #skivvy-test +b *!*Skivvy0x@*.users.quakenet.org
	//                    to: #skivvy-test
	//                  text: End of /NAMES list.
	// msg.from_channel()   : true
	// msg.get_nick()       : SooKee
	// msg.get_user()       : ~SooKee
	// msg.get_host()       : SooKee.users.quakenet.org
	// msg.get_userhost()   : ~SooKee@SooKee.users.quakenet.org
	// msg.get_user_cmd()   : End
	// msg.get_user_params(): of /NAMES list.
	// msg.reply_to()       : #skivvy-test
	// -----------------------------------------------------

	// :SooKee!~SooKee@SooKee.users.quakenet.org MODE #skivvy-test +b *!*Skivvy@*.users.quakenet.org

	str chan, flag, user;

	if(!(siss(msg.params) >> chan >> flag >> user))
	{
		log("BAD message");
		return false;
	}

	//     from: Q!TheQBot@CServe.quakenet.org
	//      cmd: MODE
	//   params: #skivvy-admin +o Skivvy00
	//       to: #skivvy-admin
	//     text: End of /NAMES list.
	// msg.from_channel()   : true
	// msg.get_nick()       : Q
	// msg.get_user()       : TheQBot
	// msg.get_host()       : CServe.quakenet.org
	// msg.get_userhost()   : TheQBot@CServe.quakenet.org
	// msg.get_user_cmd()   : End
	// msg.get_user_params(): of /NAMES list.
	// msg.reply_to()       : #skivvy-admin
	// -----------------------------------------------------

	str tb_chan = bot.get("chanops.takover.chan");
	bug_var(tb_chan);

	if(chan == tb_chan) // take back channel?
	{
		log("THIS IS A TAKE BACK CHANNEL: " << tb_chan);
		// did I just get ops?
		if(msg.params == tb_chan + " +o " + bot.nick)
		{
			log("TAKEING BACH THE CHANNEL: " << tb_chan);
			lock_guard lock(nicks_mtx);

			for(const str& nick: tb_ops)
				irc->kick({chan}, {nick}, bot.get(TAKEOVER_KICK_MSG, TAKEOVER_KICK_MSG_DEFAULT));

			// ban who you need to ban
			for(const str& mask: bot.get_vec(CHANOPS_TAKEOVER_BAN))
				irc->mode(chan, " +b " + mask);

			// ban all ops nicks
			for(const str& nick: tb_ops)
				irc->mode(chan, " +b " + nick + "!*@*");

			// open channel
			irc->mode(chan, " -i");
			irc->mode(chan, " -p");

			if(bot.has(CHANOPS_TAKEOVER_KEY))
				irc->mode(chan, " -k " + bot.get(CHANOPS_TAKEOVER_KEY));
		}
	}

	bug_var(chan);
	bug_var(flag);
	bug_var(user);

	str_vec v = bot.get_vec("chanops.protect");
	bug_var(v.size());
	for(const str& s: v)
	{
		bug_var(s);
		str chan_preg, who;
		std::istringstream(s) >> chan_preg >> who;
		bug_var(chan_preg);
		bug_var(who);
		if(bot.preg_match(chan_preg, msg.to, true) && bot.wild_match(user, who))
		{
			bug("match:");
			if(flag == "+b")
				flag = "-b";
			else if(flag == "-o")
				flag = "+b";
			else if(flag == "-v")
				flag = "+v";
			irc->mode(chan, flag , who);
		}
	}

	return true;
}

bool ChanopsIrcBotPlugin::kick_event(const message& msg)
{
	BUG_COMMAND(msg);
	//                  line: :SooKee!~SooKee@SooKee.users.quakenet.org KICK #skivvy-test Monixa :Monixa
	//                  from: SooKee!~SooKee@SooKee.users.quakenet.org
	//                   cmd: KICK
	//                params: #skivvy-test Monixa
	//                    to: #skivvy-test
	//                  text: Reason for kick
	// msg.from_channel()   : true
	// msg.get_nick()       : SooKee
	// msg.get_user()       : ~SooKee
	// msg.get_host()       : SooKee.users.quakenet.org
	// msg.get_userhost()   : ~SooKee@SooKee.users.quakenet.org
	// msg.get_user_cmd()   : Monixa
	// msg.get_user_params():
	// msg.reply_to()       : #skivvy-test

	str who;
	if(!(siss(msg.params) >> who >> who))
		return false;

	str_vec responses = bot.get_vec("chanops.kick.response");
	if(responses.empty())
		return true;

	str response = responses[rand_int(0, response.size() - 1)];
	if(response.empty())
		return true;

	for(siz i = 0; i < response.size(); ++i)
		if(response[i] == '*')
			if(!i || response[i - 1] != '\\')
				response.replace(i, 1, who);

	return irc->me(msg.to, response);
}

bool ChanopsIrcBotPlugin::join_event(const message& msg)
{
	BUG_COMMAND(msg);

	// Auto OP/VOICE/MODE/bans etc..

	enforce_static_rules(msg.to, msg.from, msg.get_nick());
	enforce_dynamic_rules(msg.to, msg.from, msg.get_nick());

	for(const str& chan: bot.get_vec(GREET_JOINERS_VEC))
	{
		if(!msg.from_channel() || msg.to != chan)
			continue;

		str_vec ungreets;
		std::ifstream ifs(bot.getf(UNGREET_FILE, UNGREET_FILE_DEFAULT));

		str ungreet;
		while(ifs >> ungreet)
			ungreets.push_back(ungreet);

		// greet only once
		str_set greeted = store.get_set("greeted");

		if(((ungreets.empty()
		|| stl::find(ungreets, msg.get_nick()) == ungreets.end() // deprecated
		|| stl::find(greeted, msg.get_userhost()) == greeted.end())
		&& msg.get_nick() != bot.nick))
		{
			str_vec greets = bot.get_vec(GREETINGS_VEC);
			if(!greets.empty())
			{
				siz min_delay = bot.get(GREET_MIN_DELAY, GREET_MIN_DELAY_DEFAULT);
				siz max_delay = bot.get(GREET_MAX_DELAY, GREET_MAX_DELAY_DEFAULT);
				str greet = greets[rand_int(0, greets.size() - 1)];

				for(siz pos = 0; (pos = greet.find("*")) != str::npos;)
					greet.replace(pos, 1, msg.get_nick());

				// greet only once
				ungreets.push_back(msg.get_nick());
				store.add("greeted", msg.get_userhost());

				std::async(std::launch::async, [&,msg,greet,min_delay,max_delay]
				{
					std::this_thread::sleep_for(std::chrono::seconds(rand_int(min_delay, max_delay)));
					bot.fc_reply(msg, greet); // irc->say(msg.to) ?
				});
			}
		}
	}

	return true;
}


bool ChanopsIrcBotPlugin::enforce_rules(const str& chan, const str& nick)
{
	for(const str& b: store.get_keys_if_wild("ban.nick.*"))
		if(store.get(b) == nick) // are then in channel?
			return kickban(chan, nick);

	for(const str& b: store.get_keys_if_wild("ban.pcre.*"))
		if(!nicks[nick].empty() && bot.preg_match(b, nicks[nick]))
			return kickban(chan, nick);

	return true;
}

bool ChanopsIrcBotPlugin::enforce_rules(const str& chan)
{
	for(const nick_pair& np: nicks)
		enforce_rules(chan, np.first);

	return true;
}

bool ChanopsIrcBotPlugin::enforce_dynamic_rules(const str& chan, const str& userhost, const str& nick)
{
//	str_vec nicks = store.get_vec("ban.nick." + chan);
//	for(const str& nick_match: nicks)
//		if(nick_match == nick)
//			return kickban(chan, nick);
//
//	str_vec pregs = store.get_vec("ban.preg." + msg.to);
//	for(const str& preg: pregs)
//		if(bot.preg_match(preg, msg.get_userhost()))
//		{
//			bot.fc_reply(msg, "USERHOST: " + msg.get_user() + " is banned from this channel.");
//			irc->mode(msg.to, "+b *!*" + msg.get_userhost());
//			irc->kick({msg.to}, {msg.get_user()}, "Bye bye.");
//			return true;
//		}
	return true;
}

bool ChanopsIrcBotPlugin::enforce_static_rules(const str& chan, const str& userhost, const str& nick)
{
	bug_func();
	bug_var(chan);
	bug_var(userhost);
	bug_var(nick);
	str chan_pattern, who_pattern;

	str why;
	// pcre kicks
	for(const str& s: bot.get_vec(CHANOPS_PCRE_KICK_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, userhost))
				irc->kick({chan}, {nick}, why);

	// wild kicks
	for(const str& s: bot.get_vec(CHANOPS_WILD_KICK_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, userhost))
				irc->kick({chan}, {nick}, why);

	str mode;
	// pcre modes
	for(const str& s: bot.get_vec(CHANOPS_PCRE_MODE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern >> mode)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, userhost))
				irc->mode(chan, mode , nick);

	// wild modes
	for(const str& s: bot.get_vec(CHANOPS_WILD_MODE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern >> mode)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, userhost))
				irc->mode(chan, mode , nick);

	// pcre ops
	for(const str& s: bot.get_vec(CHANOPS_PCRE_OP_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, userhost))
				irc->mode(chan, "+o" , nick);

	// wild ops
	for(const str& s: bot.get_vec(CHANOPS_WILD_OP_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, userhost))
				irc->mode(chan, "+o" , nick);

	// pcre voices
	for(const str& s: bot.get_vec(CHANOPS_PCRE_VOICE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, userhost))
				irc->mode(chan, "+v", nick);

	// wild voices
	for(const str& s: bot.get_vec(CHANOPS_WILD_VOICE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, userhost))
				irc->mode(chan, "+v", nick);

	return true;
}

}} // sookee::ircbot

