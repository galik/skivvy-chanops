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
PLUGIN_INFO("chanops", "Channel Operations", "1.0-beta");

using namespace skivvy;
using namespace sookee;
using namespace skivvy::irc;
using namespace sookee::types;
using namespace skivvy::utils;
using namespace sookee::utils;

const str STORE_FILE = "chanops.store.file";
const str STORE_FILE_DEFAULT = "chanops-store.txt";

//static const str BAN_FILE = "chanops.ban.file";
//static const str BAN_FILE_DEFAULT = "chanops-bans.txt";
//
//static const str USER_FILE = "chanops.user.file";
//static const str USER_FILE_DEFAULT = "chanops-users.txt";

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

const str CHANOPS_WILD_DEOP_VEC = "chanops.wild.deop";

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

std::ostream& operator<<(std::ostream& os, const ChanopsIrcBotPlugin::user_t& u)
{
//	std::time_t login_time;
//	str userhost; // <ircuser>@<host>
//	str user; // the user by which we logged in as
//	str nick; // current nick
//	str_set groups;
	// {<login_time>:<userhost>:<user>:<nick>:group1,group2}

	soss oss;
	oss << u.login_time << ':' << u.userhost << ':' << u.user << ':' << u.nick << ':';
	str sep;
	for(const str& g: u.groups)
		{ oss << sep << g; sep = ","; }
	os << '{' << escaped(oss.str()) << '}';

	return os;
}
std::istream& operator>>(std::istream& is, ChanopsIrcBotPlugin::user_t& u)
{

	// {<login_time>:<userhost>:<user>:<nick>:group1,group2}

	str o;
	getobject(is, o);
	unescape(o);
	siss iss(o);
	(iss >> u.login_time).ignore();
	sgl(iss, u.userhost, ':');
	sgl(iss, u.user, ':');
	sgl(iss, u.nick, ':');
	str group;
	while(sgl(iss, group, ','))
		u.groups.insert(group);
	return is;
}

ChanopsIrcBotPlugin::ChanopsIrcBotPlugin(IrcBot& bot)
: BasicIrcBotPlugin(bot)
, smtp("outbound.mailhop.org")
, store(bot.getf(STORE_FILE, STORE_FILE_DEFAULT))
{
	smtp.mailfrom = "<noreply@sookee.dyndns.org>";
}

ChanopsIrcBotPlugin::~ChanopsIrcBotPlugin() {}

// Permissions:
//     +v - Enables use of the voice/devoice commands.
//     +V - Enables automatic voice.
//     +o - Enables use of the op/deop commands.
//     +O - Enables automatic op.
//     +s - Enables use of the set command.
//     +i - Enables use of the invite and getkey commands.
//     +r - Enables use of the unban command.
//     +R - Enables use of the recover, sync and clear commands.
//     +f - Enables modification of channel access lists.
//     +t - Enables use of the topic and topicappend commands.
//     +A - Enables viewing of channel access lists.
//     +S - Marks the user as a successor.
//     +F - Grants full founder access.
//     +b - Enables automatic kickban.
//     +e - Exempts from +b and enables unbanning self.

// Syntax: FLAGS <channel> [target] [flags]
// Entry Nickname/Host          Flags
// ----- ---------------------- -----
// 1     Galik                  +AFRefiorstv (FOUNDER) (#autotools) [modified 5y 51w 4d ago, on Feb 18 18:45:37 2009]
// 2     jbailey                +AFRefiorstv (FOUNDER) (#autotools) [modified 5y 51w 4d ago, on Feb 18 19:49:25 2009]

// /msg chanserv flags #autotools Galik
// -ChanServ- Flags for Galik in #autotools are +AFRefiorstv.
bool ChanopsIrcBotPlugin::auto_login(const message& msg)
{
	irc->say("chanserv", "flags " + msg.get_chan() + " " + msg.get_nick());
	return true;
}

bool ChanopsIrcBotPlugin::permit(const message& msg)
{
	BUG_COMMAND(msg);

	auto found = perms.find(msg.get_user_cmd());

	if(found == perms.end())
	{
		// not accessible via exec(msg);
		bot.fc_reply_pm(msg, "ERROR: This command has no permit.");
		return false;
	}

	const str group = found->second;

	// anyone can call
	if(group == G_ANY)
		return true;

	bool in = false;
	for(const user_t& u: users)
	{
		bug_var(u.user);
		bug_var(u.userhost);
		if(u.userhost != msg.get_userhost())
			continue;
		in = true;
		bug_var(group);
		for(const str& g: u.groups)
		{
			bug_var(g);
			if(g == group)
				return true;
		}
		break;
	}

	if(in)
		bot.fc_reply_pm(msg, "ERROR: You do not have sufficient access.");
	else
		bot.fc_reply_pm(msg, "You are not logged in.");

	return false;
}

const str DEFAULT_EMAIL =
R"(Hi $USER,

This is an automated sign-up email from $BOTNAME.

Your username is $USER
Your password is $PASS

Log in to $BOTNAME like this: /msg $BOTNAME login $USER $PASS

Regards,

- $BOTNAME
)";


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

	str body = DEFAULT_EMAIL;
	sifs ifs(bot.getf("chanops.email.template", "chanops-email-template.txt"));
	if(!ifs)
		log("Unable to open email template: " << bot.getf("chanops.email.template", "chanops-email-template.txt"));
	else
	{
		char c;
		while(ifs.get(c))
			body += c;

	}

	str boundary = gen_boundary();

	replace(body, "$SUBJECT", "Skivvy's Email Signnup");
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
	bug_var(user_key);

	if(!store.has(user_key))
		return bot.cmd_error_pm(msg, "ERROR: Username not found.");

	user_r ur;
	ur = store.get(user_key, ur);
	bug("got ur");

	if(ur.groups.count(G_BANNED))
		return bot.cmd_error_pm(msg, "ERROR: Banned");

	if(ur.sum != sum)
		return bot.cmd_error_pm(msg, "ERROR: Bad password");

	user_t u(msg, ur);
	bug("got u");
	bug_var(u.nick);
	bug_var(u.user);
	bug_var(u.userhost);

	user_set::iterator ui;
	if((ui = users.find(u)) != users.end() && ui->userhost == u.userhost)
		return bot.cmd_error_pm(msg, "You are already logged in to " + bot.nick);

	u.login_time = std::time(0);
	users.erase(u);
	users.insert(u);

	if(ui != users.end()) // relogin new userhost
	{
		bug("got ui");
		bug_var(ui->nick);
		bug_var(ui->user);
		bug_var(ui->userhost);

		str_vec pusers = store.get_vec("logged-in");
		for(str& puser: pusers)
			if(siss(puser) >> u && u.user == ui->user)
				{ soss oss; oss << *ui; puser = oss.str(); }
		store.set_from("logged-in", pusers);
	}
	else
	{
		soss oss;
		oss << u;
		store.add("logged-in", oss.str());
	}

	str sep;
	soss oss;
	for(const str& group: u.groups)
	{
		oss << sep << group;
		sep = ", ";
	}

//	bot.fc_reply_pm(msg, "You are now logged in to " + bot.nick + ": " + oss.str());
	bot.fc_reply_notice(msg, "You are now logged in to " + bot.nick + ": " + oss.str());

	apply_acts(u);

	return true;
}

bool args_check(const str_vec& args, uns size, const str& err)
{
	if(!(args.size() == size))
	{
		log("ERROR: wrong number of parameters: " << args.size() << " expected: " << err);
		return false;
	}
	return true;
}

str_vec ChanopsIrcBotPlugin::api(unsigned call, const str_vec& args)
{
	if(call == ChanopsApi::is_userhost_logged_in)
	{
		if(args_check(args, 1, "(str userhost)"))
			if(is_userhost_logged_in(args[0]))
				return {"true"};
		return {};
	}
	else if(call == ChanopsApi::get_userhost_username)
	{
		if(args_check(args, 1, "(str userhost)"))
			return {get_userhost_username(args[0])};
	}
	else if(call == ChanopsApi::set_user_prop)
	{
		if(args_check(args, 3, "(str username, str key, str val)"))
			set_user_prop(args[0], args[1], args[2]);
	}
	else if(call == ChanopsApi::get_user_prop)
	{
		if(args_check(args, 2, "(str username, str key)"))
			return {get_user_prop(args[0], args[1])};
	}

	return {};
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

void ChanopsIrcBotPlugin::set_user_prop(const str& username, const str& key, const str& val)
{
	store.set("user." + username + "." + key, val);
}

str ChanopsIrcBotPlugin::get_user_prop(const str& username, const str& key)
{
	return store.get("user." + username + "." + key);
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
	(void) user;
	(void) group;
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

bool ChanopsIrcBotPlugin::banlist(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!~SooKee@SooKee.users.quakenet.org PRIVMSG #skivvy :!banlist #1
	//                prefix: SooKee!~SooKee@SooKee.users.quakenet.org
	//               command: PRIVMSG
	//                params:  #skivvy :!banlist #1
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : ~SooKee
	// get_host()           : SooKee.users.quakenet.org
	// param                : #skivvy
	// param                : !banlist #1
	// middle               : #skivvy
	// trailing             : !banlist #1
	// get_nick()           : SooKee
	// get_chan()           : #skivvy
	// get_user_cmd()       : !banlist
	// get_user_params()    : #1
	//---------------------------------------------------

	// Make this channel specific

	static const str prompt = prompt_color("banlist");

	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error_pm(msg, prompt + "!banlist can only be used from a channel.");

	// !banlist #n

	str skip;// = lowercase(msg.get_user_params());
	siz n = 1;
	siss iss(msg.get_user_params());
	sgl(iss, skip, '#') >> n;

	if(!n)
		n = 1;

	str chan = msg.get_chan();

	lock_guard lock(store_mtx);

	str_vec bans = store.get_vec("ban");

	if(bans.empty())
		bot.fc_reply_pm(msg, prompt + " there are no bans.");
	else
	{
		const siz size = bans.size();
		const siz start = (n - 1) * 10;
		const siz end = (start + 10) > size ? size : (start + 10);

		bug_var(size);
		bug_var(start);
		bug_var(end);

		bot.fc_reply_pm(msg, prompt + IRC_UNDERLINE + IRC_BOLD + "Listing #" + std::to_string(n)
			+ " of " + std::to_string((size + 9)/10)
			+ IRC_NORMAL + " (from " + std::to_string(start + 1) + " to "
			+ std::to_string(end) + " of " + std::to_string(size) + ")");

		for(siz i = start; i < end; ++i)
		{
			soss oss;
			oss << IRC_BOLD << (i + 1) << ": " << IRC_NORMAL << bans[i];
			bot.fc_reply_pm(msg, prompt + oss.str());
		}
	}

	return true;
}

bool ChanopsIrcBotPlugin::unban(const message& msg)
{
	BUG_COMMAND(msg);
	static const str prompt = prompt_color("unban");

	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error_pm(msg, prompt + "!unban can only be used from a channel.");

	// Make this channel specific

	// !unban <range-list>

	siz_vec items;
	if(!parse_rangelist(msg.get_user_params(), items))
		return bot.cmd_error(msg, prompt + bot.help(msg.get_user_cmd()));

	str_vec newbans;
	lock_guard lock(store_mtx);
	const str_vec bans = store.get_vec("ban");
	for(siz i = 0; i < bans.size(); ++i)
		if(stl::find(items, i + 1) == items.cend())
			newbans.push_back(bans[i]);
		else
		{
			bot.fc_reply(msg, prompt + IRC_BOLD + std::to_string(i + 1) + ": "
				+ IRC_NORMAL + bans[i]);

			str chan, who;
			if(siss(bans[i]) >> chan >> who)
				irc->mode(chan, " -b " + who);
		}

	store.clear("ban");
	store.set_from("ban", newbans);

	return true;
}

bool ChanopsIrcBotPlugin::ban(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!angelic4@192.168.0.54 PRIVMSG #skivvy :!ban Monixa
	//                prefix: SooKee!angelic4@192.168.0.54
	//               command: PRIVMSG
	//                params:  #skivvy :!ban Monixa
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : angelic4
	// get_host()           : 192.168.0.54
	// param                : #skivvy
	// param                : !ban Monixa
	// middle               : #skivvy
	// trailing             : !ban Monixa
	// get_nick()           : SooKee
	// get_chan()           : #skivvy
	//---------------------------------------------------

	// !ban Nick *(+<type>)
	// if not type given uses nick-ban
	// types:
	//        nick - <nick>!*@*
	//        user - *!*<user>@*
	//        host - *!*@*<host>
	// If user/host info not available defaults to nick-ban

	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error_pm(msg, "ERROR: !ban can only be used from a channel.");

	siss iss(msg.get_user_params());
	str chan = msg.get_chan();
	str nick;
	iss >> nick;

	bug_var(chan);
	bug_var(nick);

	lock_guard lock(store_mtx);
	// <nick>|<wild>
	if(bot.nicks[chan].count(nick)) // ban by nick
	{
		bool nick_ban = false;
		bool user_ban = false;
		bool host_ban = false;

		str flag;
		while((iss >> std::ws).peek() == '+' && iss >> flag)
		{
			if(flag == "+nick")
				nick_ban = true;
			else if(flag == "+user")
				user_ban = true;
			else if(flag == "+host")
				host_ban = true;
		}

		str reason;
		if(!sgl(iss, reason))
			reason = "Banned by " + msg.get_nickname() + ": " + reason;
		bug_var(reason);

		if(!nick_ban && !user_ban && !host_ban)
			nick_ban = true;

		str user;
		str host;

		ircuser_set_iter i;
		if((i = find_by_nick(ircusers, nick)) != ircusers.end())
		{
			user = i->user;
			host = i->host;
		}


		if(user.empty() || host.empty())
			nick_ban = !(user_ban = (host_ban = false));

//		if(user_ban || host_ban)
//			nick_ban = false;

		bug_var(nick_ban);
		bug_var(user_ban);
		bug_var(host_ban);

		str wild;
		if(nick_ban)
			wild += nick + "!";
		else
			wild += "*!";

		if(user_ban)
			wild += "*" + user + "@";
		else
			wild += "*@";

		if(host_ban)
			wild += "*" + host;
		else
			wild += "*";

		bug_var(wild);

		store.add("ban", chan + " " + wild + " " + reason);
		irc->mode(chan, " +b " + wild);
		irc->kick({chan}, {nick}, reason);
	}
	else // not a nick, assume nick is a wildcard on prefix
	{
		store.add("ban", chan + " " + nick);
		irc->mode(chan, " +b " + nick);
	}

	return true;
}

bool ChanopsIrcBotPlugin::heard(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!angelic4@192.168.0.54 PRIVMSG #skivvy :!ban Monixa
	//                prefix: SooKee!angelic4@192.168.0.54
	//               command: PRIVMSG
	//                params:  #skivvy :!heard Monixa
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : angelic4
	// get_host()           : 192.168.0.54
	// param                : #skivvy
	// param                : !ban Monixa
	// middle               : #skivvy
	// trailing             : !ban Monixa
	// get_nick()           : SooKee
	// get_chan()           : #skivvy
	//---------------------------------------------------

	static const str prompt = IRC_BOLD + IRC_COLOR + IRC_Yellow + "heard"
		+ IRC_COLOR + IRC_Black + ": " + IRC_NORMAL;

	if(!permit(msg))
		return false;

	str nick = msg.get_user_params();

	str_set keys;
	if(store.has("heard." + lower_copy(nick)))
		keys.insert("heard." + lower_copy(nick));
	else
	{
		keys = store.get_keys_if_wild("heard." + lower_copy(nick));
	}

	if(keys.empty())
		bot.fc_reply(msg, prompt + nick + " does not match any names");
	else if(keys.size() > 4)
		bot.fc_reply(msg, prompt + nick + " matches too many names");
	else
	{
		for(const auto& key: keys)
		{
//			if(key.size() > sizeof("heard."))
//				nick = key.substr(sizeof("heard.") - 1);
			str info = store.get(key);
			bug_var(info);

			std::time_t utime;
			str chan, text;
			if(!sgl(siss(info) >> nick >> chan >> utime >> std::ws, text))
			{
				bot.fc_reply(msg, prompt + "Nick " + nick + " has not been heard.");
				return true;
			}

			soss oss;
			print_duration(st_clk::now() - st_clk::from_time_t(utime), oss);

			str time = oss.str();
			trim(time);
			bot.fc_reply(msg, prompt + IRC_BOLD + nick
				+ IRC_NORMAL + " was last heard "
				+ IRC_BOLD + time
				+ IRC_NORMAL+ " ago in "
				+ IRC_BOLD + chan
				+ IRC_NORMAL + " saying: \""
				+ IRC_BOLD + text
				+ IRC_NORMAL + "\"");
		}
	}

	return true;
}

bool ChanopsIrcBotPlugin::tell(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!angelic4@192.168.0.54 PRIVMSG #skivvy :!ban Monixa
	//                prefix: SooKee!angelic4@192.168.0.54
	//               command: PRIVMSG
	//                params:  #skivvy :!heard Monixa
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : angelic4
	// get_host()           : 192.168.0.54
	// param                : #skivvy
	// param                : !ban Monixa
	// middle               : #skivvy
	// trailing             : !ban Monixa
	// get_nick()           : SooKee
	// get_chan()           : #skivvy
	//---------------------------------------------------

//	static const str prompt = IRC_BOLD + IRC_COLOR + IRC_Green + "tell"
//		+ IRC_COLOR + IRC_Black + ": " + IRC_NORMAL;

	const str prompt = prompt_color("tell");

	if(!permit(msg))
		return false;

	str nickname = msg.get_nickname();
	str userhost = msg.get_userhost();
	str chan = msg.get_chan();
	str nick, text; // who & what to tall

	siss iss(msg.get_user_params());
	if(!sgl(iss >> nick >> std::ws, text))
	{
		log("ERROR: chanops: Bad tell message: " << msg.line);
		return false;
	}

	if(!nickname.empty() && !userhost.empty())
		store.add("tell." + nick, nickname + " " + chan + " " + std::to_string(std::time(0))
		+ " " + text);

	bot.fc_reply(msg, prompt + nick + " will be told when he next speaks.");
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

// mode #channel -o+b Victim *!*@Victims-ip-and.isp.net

bool ChanopsIrcBotPlugin::votekick(const message& msg)
{
	BUG_COMMAND(msg);
	if(!permit(msg))
		return false;

	if(!msg.from_channel())
		return bot.cmd_error(msg, "You can only call a vote from a channel.");

	str chan = msg.get_chan();

	lock_guard lock(vote_mtx);

	if(vote_in_progress[chan])
		return bot.cmd_error(msg, "Vote already in progress.", true);

	// TODO: track nick using userhost

	str nick;
	str reason;
	siss iss(msg.get_user_params());
	if(!(sgl(iss >> nick >> std::ws, reason)))
		return bot.cmd_error(msg, "usege: !votekick <nick> <reason> *(<duration>)");

	ircuser_set_iter i;
	if((i = find_by_nick(ircusers, nick)) == ircusers.end())
		return bot.cmd_error(msg, "Nick " + nick + " not found.");

	str user = i->user;

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
	vote_fut[chan] = std::async(std::launch::async, [=]{ ballot(chan, user, nick, st_clk::now() + std::chrono::seconds(secs)); });

	return true;
}

bool ChanopsIrcBotPlugin::f1(const message& msg)
{
	BUG_COMMAND(msg);

	if(!msg.from_channel())
		return bot.cmd_error(msg, "You can only vote from a channel.");

	str chan = msg.get_chan();
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

	str chan = msg.get_chan();
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

bool ChanopsIrcBotPlugin::ballot(const str& chan, const str& user, const str& oldnick, const st_time_point& end)
{

	while(st_clk::now() < end)
		std::this_thread::sleep_until(end);

	lock_guard lock(vote_mtx);

	bug_func();
	bug_var(chan);
	bug_var(user);
	bug_var(oldnick);

	vote_in_progress[chan] = false;
	bug_var(vote_f1[chan]);
	bug_var(vote_f2[chan]);

	ircuser_set_iter i;
	if((i = find_by_user(ircusers, user)) == ircusers.end())
	{
		irc->say(chan, bold + red + "VOTE-KICK: " + blue + "Oh dear "
			+ black + oldnick + blue + " seems to have disapeared!");
		return true;
	}

	bug_var(i->nick);
	bug_var(i->user);
	bug_var(i->host);

	if(vote_f1[chan] > vote_f2[chan])
	{
		irc->say(chan, bold + red + "VOTE-KICK: " + black
			+ i->nick + blue + " : The people have spoken and it was not good "
			+ black + std::to_string(vote_f1[chan]) + " - "
			+ std::to_string(vote_f2[chan]) + blue + " to kick!"
			+ black + " :'(");
		irc->kick({chan}, {i->nick}, "You are the weakest link.... goodby!");
	}
	else
		irc->say(chan, bold + red + "VOTE-KICK: " + blue + "The scrawney life of "
			+ black + i->nick + blue + " has been saved by the people!");

	return true;
}

bool ChanopsIrcBotPlugin::cookie(const message& msg, int num)
{
	BUG_COMMAND(msg);
	bug_var(num);
//	bug_var(permit(msg));

	if(!permit(msg))
		return false;

	// num: +1 = give, -1 = take,  0 = count

	str nick = msg.get_user_params();

	if(num)
	{
		store.set("cookies." + nick, store.get("cookies." + nick, 0) + num);
		if(num < 0)
			bot.fc_reply(msg, REPLY_PROMPT + msg.get_nickname() + " has taken "
				+ std::to_string(-num) + " cookie" + (-num>1?"s":"") + " from " + nick + ".");
		else
			bot.fc_reply(msg, REPLY_PROMPT + msg.get_nickname() + " has given "
				+ std::to_string(num) + " cookie" + (num>1?"s":"") + " to " + nick + ".");
		return true;
	}

	int n = store.get("cookies." + nick, 0);
	bot.fc_reply(msg, REPLY_PROMPT + nick + " has " + std::to_string(n) + " cookie" + ((n==1||n==-1)?"":"s"));

	return true;
}

// INTERFACE: BasicIrcBotPlugin

bool ChanopsIrcBotPlugin::initialize()
{
	bug_func();
//	std::ifstream ifs(bot.getf(BAN_FILE, BAN_FILE_DEFAULT));
	perms =
	{
		{"!users", G_USER}
		, {"!reclaim", G_USER}
		, {"!ban", G_OPER}
		, {"!banlist", G_OPER}
		, {"!unban", G_OPER}
		, {"!votekick", G_OPER}
		, {"!heard", G_ANY}
		, {"!tell", G_ANY}
		, {"!cookies", G_ANY}
		, {"!cookie++", G_OPER}
		, {"!cookie--", G_OPER}
	};

	// chanops.init.user: <user> <pass> <PERM> *( "," <PERM> )
	auto init_users = bot.get_vec("chanops.init.user");
	bug_var(init_users.size());
//	for(const str& init: init_users)
	for(siz i = 0; i < init_users.size(); ++i)
	{
		str user, pass, email, list;

		str init = init_users[i];
		bug_var(init);

		if(!sgl(siss(init) >> user >> pass >> email >> std::ws, list))
			continue;

		bug_var(user);
		bug_var(pass);
		bug_var(email);

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

	// Update store version
	uns version = store.get("version", 0U);

	if(version == 0)
	{
		// upgrade to #1
		str_set keys = store.get_keys_if_wild("heard.*");
		for(const auto& key: keys)
		{
			str nick = "<none>";
			if(key.size() > sizeof("heard."))
				nick = key.substr(sizeof("heard.") - 1);
			store.set(key, nick + ' ' + store.get(key, ""));
		}
		++version;
		store.set("version", version);
	}


	// persistent logins
	user_t u;
	const str_vec logged_in = store.get_vec("logged-in");
	for(const str& user: logged_in)
		if(siss(user) >> u)
		{
			bug("USER: " << user << " is persistently logged-in.");
			users.insert(u);
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
		, "register <username> <email@address> <email@address>"
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
		"!heard"
		, "!heard <nick> - When did <nick> last say something?"
		, [&](const message& msg){ heard(msg); }
	});
	add
	({
		"!tell"
		, "!tell <nick> <message> - Tell someone somthing after they next speak in channel."
		, [&](const message& msg){ tell(msg); }
	});
	add
	({
		"!ban"
		, "!ban <nick>|<wildcard> - ban either a registered user OR a wildecard match on user prefix."
		, [&](const message& msg){ ban(msg); }
	});
	add
	({
		"!banlist"
		, "!banlist List bans for the channel by number."
		, [&](const message& msg){ banlist(msg); }
	});
	add
	({
		"!unban"
		, "!unban 2-4,7,9,11-15 - remove all the bans coresponding to the list of numbers from !banlist."
		, [&](const message& msg){ unban(msg); }
	});
	add
	({
		"!cookies"
		, "!cookies <nick> Show <nick>'s cookies."
		, [&](const message& msg){ cookie(msg, 0); }
	});
	add
	({
		"!cookie++"
		, "!cookie++ <nick> Give <nick> a cookie."
		, [&](const message& msg){ cookie(msg, 1); }
	});
	add
	({
		"!cookie--"
		, "!cookie-- <nick> Take a cookie from <nick>."
		, [&](const message& msg){ cookie(msg, -1); }
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
//
//	bug("DUMPUNG USERS: - before");
//	for(const ircuser& u: ircusers)
//	{
//		bug_var(u.nick);
//		bug_var(u.user);
//		bug_var(u.host);
//		bug("-----------------------------");
//	}

//	enforce_static_rules(msg.get_chan(), msg.prefix, msg.get_nickname());
//	enforce_dynamic_rules(msg.get_chan(), msg.prefix, msg.get_nickname());

	// update nicks if appropriate
	str nickname = msg.get_nickname();
	str user = msg.get_user();
	str host = msg.get_host();

	// debugging info
	auto found = find_by_user(ircusers, user);
	if(found != ircusers.end())
	{
		bug("= ircuser =======================");
		bug_var(found->nick);
		bug_var(found->flag);
	}

	if(!nickname.empty() && !user.empty() && !host.empty())
	{
		lock_guard lock(ircusers_mtx);
		ircuser u;
		auto found = find_by_user(ircusers, user);
		if(found != ircusers.end())
			u = *found;

		u.nick = nickname;
		u.user = user;
		u.host = host;

		ircusers.erase(u);
		ircusers.insert(u);

		// every 10 minutes update a database
		if(st_clk::now() > ircuser_update)
		{
			ircuser u;
			ircuser_vec db;

			std::ifstream ifs(bot.getf("chanops.ircuser.file", "chanops-ircuser-db.txt"));

			str line;
			while(sgl(ifs, line))
				if(siss(line) >> u)
					db.push_back(u);
			ifs.close();
			for(const ircuser& u: ircusers)
				if(std::find_if(db.begin(), db.end(), [&](const ircuser& iu){return iu == u;}) == db.end())
					db.push_back(u);

			std::sort(db.begin(), db.end(), [](const ircuser& u1, const ircuser& u2){return u1.host + u1.user + u1.nick < u2.host + u2.user + u2.nick;});
			ircuser_vec_iter end = std::unique(db.begin(), db.end());
			db.erase(end, db.end());

			std::ofstream ofs(bot.getf("chanops.ircuser.file", "chanops-ircuser-db.txt"));

			str sep;
			for(const ircuser& u: db)
				{ ofs << sep << (sss() << u).str(); sep = "\n"; }

			ircuser_update = st_clk::now() + std::chrono::minutes(10);
		}
	}

	if(msg.command == PRIVMSG)
		talk_event(msg);
	else if(msg.command == RPL_NAMREPLY)
		name_event(msg);
	else if(msg.command == NICK)
		nick_event(msg);
	else if(msg.command == RPL_WHOISUSER || msg.command == RPL_WHOISCHANNELS || msg.command == RPL_WHOISOPERATOR)
		whoisuser_event(msg);
	else if(msg.command == JOIN)
		join_event(msg);
//	else if(msg.command == QUIT)
//		quit_event(msg);
	//	else if(msg.command == PART)
	//		part_event(msg);
	else if(msg.command == MODE)
		mode_event(msg);
	else if(msg.command == KICK)
		kick_event(msg);
	else if(msg.command == NOTICE)
		notice_event(msg);

//	bug("DUMPUNG USERS: - after");
//	for(const ircuser& u: ircusers)
//	{
//		bug_var(u.nick);
//		bug_var(u.user);
//		bug_var(u.host);
//		bug("-----------------------------");
//	}
}

// << PRIVMSG chanserv :flags #autotools Galik
// >> :ChanServ!ChanServ@services. NOTICE Galik :+Flags for Galik in #autotools are +AFRefiorstv.

bool ChanopsIrcBotPlugin::notice_event(const message& msg)
{
	bug_func();
	bug_msg(msg);
	// -----> bool skivvy::ircbot::ChanopsIrcBotPlugin::notice_event(const skivvy::ircbot::message&) [1] {8: 0x7f2784c47950}
	//                  line: :ChanServ!ChanServ@services. NOTICE Skivvy :Flags for Galik in #autotools are +AFRefiorstv.
	//                prefix: ChanServ!ChanServ@services.
	//               command: NOTICE
	//                params:  Skivvy :Flags for Galik in #autotools are +AFRefiorstv.
	// get_servername()     :
	// get_nickname()       : ChanServ
	// get_user()           : ChanServ
	// get_host()           : services.
	// param                : Skivvy
	// param                : Flags for Galik in #autotools are +AFRefiorstv.
	// middle               : Skivvy
	// trailing             : Flags for Galik in #autotools are +AFRefiorstv.
	// get_nick()           : ChanServ
	// get_chan()           :
	// get_user_cmd()       : Flags
	// get_user_params()    : for Galik in #autotools are +AFRefiorstv.
	// <----- bool skivvy::ircbot::ChanopsIrcBotPlugin::notice_event(const skivvy::ircbot::message&) [1] {8: 0x7f2784c47950}

	// -----> bool skivvy::ircbot::ChanopsIrcBotPlugin::notice_event(const skivvy::ircbot::message&) [1] {8: 0x7f2784c47950}
	//                  line: :ChanServ!ChanServ@services. NOTICE Skivvy :No flags for zadock in #autotools.
	//                prefix: ChanServ!ChanServ@services.
	//               command: NOTICE
	//                params:  Skivvy :No flags for zadock in #autotools.
	// get_servername()     :
	// get_nickname()       : ChanServ
	// get_user()           : ChanServ
	// get_host()           : services.
	// param                : Skivvy
	// param                : No flags for zadock in #autotools.
	// middle               : Skivvy
	// trailing             : No flags for zadock in #autotools.
	// get_nick()           : ChanServ
	// get_chan()           :
	// get_user_cmd()       : No
	// get_user_params()    : flags for zadock in #autotools.
	// <----- bool skivvy::ircbot::ChanopsIrcBotPlugin::notice_event(const skivvy::ircbot::message&) [1] {8: 0x7f2784c47950}

	if(msg.get_nick() == "ChanServ")
	{
		if(msg.get_user_cmd() == "Flags")
		{
			// auto_login?
			str skip, nick, chan, flags;
			if(!(siss(msg.get_user_params()) >> skip >> nick >> skip >> chan >> skip >> flags))
				log("ERROR: parsing chanserv Flags notice: " << msg.get_user_params());
			else
			{
				trim(nick, "");
				trim(chan, "");
				trim(flags, "");

				bug_var(nick);
				bug_var(chan);
				bug_var(flags);

				if(flags.find_first_of("o") != str::npos)
				{
					bug("op found");
					user_t u;
					auto login = users.begin();

					for(; login != users.end(); ++login)
					{
						bug_var(login->nick);
						if(login->nick == nick)
							break;
					}

					if(login == users.end()) // not logged in by other means
					{
						bug("attempting to log in");
						for(auto&& iu: ircusers)
						{
							bug_var(iu.nick);
							if(iu.nick != nick)
								continue;

							bug("found iu.nick");

							u.login_time = std::time(0);
							u.userhost = iu.user + "@" + iu.host;
							u.user = iu.user;
							u.chan_flags = flags;
							u.groups = {G_USER, G_OPER};
							users.insert(u); // logged in
							log("CHANSERV AUTO LOGIN: logged in: " << u);
							break;
						}
					}
					else
					{
						// update info
						u = *login;
						users.erase(login);
						u.chan_flags = flags;
						users.insert(u);
						log("CHANSERV AUTO LOGIN: modified login: " << u);
					}
				}
			}
		}
	}

//	for(auto&& u: users)
//		if(u.nick == msg.get_nick())
	return true;
}

bool ChanopsIrcBotPlugin::talk_event(const message& msg)
{
	str nickname = msg.get_nickname();
	str userhost = msg.get_userhost();
	str chan = msg.get_chan();

	if(lower_copy(msg.get_nick()) == "chanserv")
	{
		// Is this an auto login event?
		// Flags for Galik in #autotools are +AFRefiorstv
		bug("POTENTIAL AUTO LOGIN EVENT");
		bug_msg(msg);
	}

	if(!nickname.empty() && !chan.empty() && msg.get_trailing().find("\001ACTION "))
		store.set("heard." + lower_copy(nickname)
			, nickname + ' ' + chan + ' ' + std::to_string(std::time(0))
				+ ' ' + msg.get_trailing());

	// !tell

	if(store.has("tell." + nickname))
	{
		str_vec infos = store.get_vec("tell." + nickname);
		for(const str& info: infos)
		{
	//		str info = store.get("tell." + nickname);
			time_t utime;
			str nick, chan, text;
			if(!sgl(siss(info) >> nick >> chan >> utime >> std::ws, text))
			{
				log("ERROR: chnops: broken tell record in store: " << info);
				return true;
			}

			if(chan != msg.get_chan())
				return true;

			static const str prompt = IRC_BOLD + IRC_COLOR + IRC_Hot_Pink + "tell"
				+ IRC_COLOR + IRC_Black + ": " + IRC_NORMAL;

			soss oss;
			print_duration(st_clk::now() - st_clk::from_time_t(utime), oss);

			str time = oss.str();
			trim(time);
			bot.fc_reply(msg, prompt + nickname + ": " + IRC_BOLD + nick
				+ IRC_NORMAL + " left you a message "
				+ IRC_BOLD + time
				+ IRC_NORMAL+ " ago: \""
				+ IRC_BOLD + text
				+ IRC_NORMAL + "\"");
		}
		store.clear("tell." + nickname);
	}

	return true;
}

bool ChanopsIrcBotPlugin::nick_event(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!~SooKee@SooKee.users.quakenet.org NICK :hidingme
	//                prefix: SooKee!~SooKee@SooKee.users.quakenet.org
	//               command: NICK
	//                params:  :hidingme
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : ~SooKee
	// get_host()           : SooKee.users.quakenet.org
	// param                : hidingme
	// trailing             : hidingme
	// get_nick()           : SooKee
	// get_chan()           :
	// get_user_cmd()       : hidingme
	// get_user_params()    :
	//---------------------------------------------------

	lock_guard lock(ircusers_mtx);
	ircuser u;
	auto found = find_by_user(ircusers, msg.get_user());
	if(found != ircusers.end())
		u = *found;

	u.nick = msg.get_nickname();
	u.user = msg.get_user();
	u.host = msg.get_host();

	ircusers.erase(u);
	ircusers.insert(u);

	return true;
}

str make_ban_mask(const str& userhost)
{
	str mask = "*!*@*";
	siz pos = userhost.rfind(".");
	bug_var(pos);
	pos = userhost.rfind(".", --pos);
	bug_var(pos);
	mask += userhost.substr(++pos);
	bug_var(mask);
	return mask;
}

bool ChanopsIrcBotPlugin::whoisuser_event(const message& msg)
{
	BUG_COMMAND(msg);
	// RPL_WHOISUSER
	//---------------------------------------------------
	//                  line: :dreamhack.se.quakenet.org 311 Skivvy SooKee ~SooKee SooKee.users.quakenet.org * :SooKee
	//                prefix: dreamhack.se.quakenet.org
	//               command: 311
	//                params:  Skivvy SooKee ~SooKee SooKee.users.quakenet.org * :SooKee
	// get_servername()     : dreamhack.se.quakenet.org
	// get_nickname()       :
	// get_user()           :
	// get_host()           :
	// param                : Skivvy
	// param                : SooKee
	// param                : ~SooKee
	// param                : SooKee.users.quakenet.org
	// param                : *
	// param                : SooKee
	// middle               : Skivvy
	// middle               : SooKee
	// middle               : ~SooKee
	// middle               : SooKee.users.quakenet.org
	// middle               : *
	// trailing             : SooKee
	// get_nick()           :
	// get_chan()           :
	// get_user_cmd()       : SooKee
	// get_user_params()    :
	//---------------------------------------------------
	// RPL_WHOISCHANNELS
	//---------------------------------------------------
	//                  line: :dreamhack.se.quakenet.org 319 Skivvy SooKee :@#skivvy @#openarenahelp +#openarena @#omfg
	//                prefix: dreamhack.se.quakenet.org
	//               command: 319
	//                params:  Skivvy SooKee :@#skivvy @#openarenahelp +#openarena @#omfg
	// get_servername()     : dreamhack.se.quakenet.org
	// get_nickname()       :
	// get_user()           :
	// get_host()           :
	// param                : Skivvy
	// param                : SooKee
	// param                : @#skivvy @#openarenahelp +#openarena @#omfg
	// middle               : Skivvy
	// middle               : SooKee
	// trailing             : @#skivvy @#openarenahelp +#openarena @#omfg
	// get_nick()           :
	// get_chan()           :
	// get_user_cmd()       : @#skivvy
	// get_user_params()    : @#openarenahelp +#openarena @#omfg
	//---------------------------------------------------

	str skip, userhost;

	str_vec params = msg.get_params();

	if(msg.command == RPL_WHOISUSER)
	{
		if(params.size() < 4)
		{
			log("MESSAGE ERROR: Expected > 3 params: " << msg.line);
			return false;
		}

		bug_var(params[1]);
		bug_var(params[2]);
		bug_var(params[3]);

		const str tb_chan = bot.get("chanops.takover.chan");
		bug_var(tb_chan);

		if(!tb_chan.empty())
		{
			lock_guard lock(chanops_mtx);
			if(chanops[tb_chan])
			{
				if(tb_ops.count(params[1]))
					irc->mode(tb_chan, " +b *!*@" + params[3]);
				kickban(tb_chan, params[1]);
			}
		}

		{
			lock_guard lock(ircusers_mtx);
			ircuser u;
			auto found = find_by_user(ircusers, params[2]);
			if(found != ircusers.end())
				u = *found;

			u.nick = params[1];
			u.user = params[2];
			u.host = params[3];

			ircusers.erase(u);
			ircusers.insert(u);
		}
	}
	else if(msg.command == RPL_WHOISCHANNELS)
	{
		if(params.size() != 3)
		{
			log("MESSAGE ERROR: Expected 3 params: " << msg.line);
			return false;
		}

		bug_var(params[0]); // caller (skivvy)
		bug_var(params[1]); // nick
		bug_var(params[2]); // chaninfo

		lock_guard lock(ircusers_mtx);
		auto found = find_by_nick(ircusers, params[1]);
		if(found != ircusers.end())
			log("ERROR: expected ircuser in db: " << msg.line);
		else
		{
			ircuser u = *found;
			siss iss(params[2]); // @#skivvy @#openarenahelp +#openarena @#omfg
			str chaninfo;
			while(iss >> chaninfo)
			{
				if(chaninfo.empty())
					continue;

				u.flag.clear();

				if(chaninfo[0] == '+')
					u.flag += 'v';
				else if(chaninfo[0] == '@')
					u.flag += 'o';
			}
			ircusers.erase(u);
			ircusers.insert(u);
		}
	}

	return true;
}

bool ChanopsIrcBotPlugin::name_event(const message& msg)
{
	BUG_COMMAND(msg);
	// RPL_NAMREPLY
	//---------------------------------------------------
	//                  line: :dreamhack.se.quakenet.org 353 Skivvy = #openarenahelp :Skivvy +wing_9 @ali3n +capo @Pixelized @SooKee +pet @Q
	//                prefix: dreamhack.se.quakenet.org
	//               command: 353
	//                params:  Skivvy = #openarenahelp :Skivvy  +wing_9 @ali3n +capo @Pixelized @SooKee +pet @Q
	// get_servername()     : dreamhack.se.quakenet.org
	// get_nickname()       :
	// get_user()           :
	// get_host()           :
	// param                : Skivvy
	// param                : =
	// param                : #openarenahelp
	// param                : Skivvy  +wing_9 @ali3n +capo @Pixelized @SooKee +pet @Q
	// middle               : Skivvy
	// middle               : =
	// middle               : #openarenahelp
	// trailing             : Skivvy  +wing_9 @ali3n +capo @Pixelized @SooKee +pet @Q
	// get_nick()           :
	// get_chan()           : #openarenahelp
	// get_user_cmd()       : Skivvy
	// get_user_params()    :  +wing_9 @ali3n +capo @Pixelized @SooKee +pet @Q
	//---------------------------------------------------

	// :dreamhack.se.quakenet.org 353 Skivvy = #openarena :Skivvy +SooKee +I4C @OAbot +Light3r +pet

	str chan = msg.get_chan();
	bug_var(chan);

	str tb_chan = bot.get("chanops.takover.chan");

	str nick;

	siss iss(msg.get_trailing());

//	lock_guard lock(nicks_mtx);
	str_set whoiss;
	while(iss >> nick)
	{
		if(!nick.empty())
		{
			if(nick == "@" + bot.nick)
				chanops[chan] = true;

			// TODO: add an exceptions list? deal with Q on QukeNet somehow...
			if(chan == tb_chan && nick[0] == '@' /*&& nick != "Q"*/ && nick != "@" + bot.nick)
				tb_ops.insert(nick.substr(1));

			if(nick[0] == '+' || nick[0] == '@')
				nick.erase(0, 1);

			whoiss.insert(nick);
//			if(ircusers.find(nick) == ircusers.end())
//			{
//				irc->whois(nick); // initiate request, see whois_event() for response
//				ircusers[nick];
//			}
		}
	}

	if(!whoiss.empty())
		irc->whois(whoiss);

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
	//---------------------------------------------------
	//                  line: :Q!TheQBot@CServe.quakenet.org MODE #openarenahelp +v Sergei
	//                prefix: Q!TheQBot@CServe.quakenet.org
	//               command: MODE
	//                params:  #openarenahelp +v Sergei
	// get_servername()     :
	// get_nickname()       : Q
	// get_user()           : TheQBot
	// get_host()           : CServe.quakenet.org
	// param                : #openarenahelp
	// param                : +v
	// param                : Sergei
	// middle               : #openarenahelp
	// middle               : +v
	// middle               : Sergei
	// trailing             :
	// get_nick()           : Q
	// get_chan()           : #openarenahelp
	// get_user_cmd()       :
	// get_user_params()    :
	//---------------------------------------------------

	// :SooKee!~SooKee@SooKee.users.quakenet.org MODE #skivvy-test +b *!*Skivvy@*.users.quakenet.org

	str chan, flag, user;

	str_vec params = msg.get_params();
	if(params.size() > 2) // could be ban/kick/voice etc...
	{
		chan = params[0];
		flag = params[1];
		user = params[2];

		str tb_chan = bot.get("chanops.takover.chan");
		bug_var(tb_chan);

		bool ops = false;
		{
			lock_guard lock(chanops_mtx);

			// did I just get ops?
			// Command: MODE
			// Parameters: <channel> *( ( "-" / "+" ) *<modes> *<modeparams> )

			// +abc NickA NickB NickC
			str_vec params = msg.get_params();
			for(siz i = 1; i < params.size(); ++i)
			{
				if(params[i].empty())
					continue;
				if(params[i][0] != '+' && params[i][0] != '-')
					continue;
				siz f;
				for(f = 1; f < params[i].size(); ++f)
					if(params[i][f] == 'o')
						if(i + f < params.size() && params[i + f] == bot.nick)
							ops = (chanops[chan] = (params[i][0] == '+'));
				i += f;
			}
			ops = chanops[chan];
		}

		// .-----------------------------------------.
		// | Everything past this point requires ops |
		// '-----------------------------------------'

		if(!ops)
			return true;

		if(chan == tb_chan) // take back channel?
		{
			log("TAKEING BACH THE CHANNEL: " << tb_chan);
			lock_guard lock(ircusers_mtx);

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

//		enforce_static_rules(msg.get_chan(), msg.prefix, msg.get_nickname());
//		enforce_dynamic_rules(msg.get_chan(), msg.prefix, msg.get_nickname());

		bug_var(chan);
		bug_var(flag);
		bug_var(user);

		str_vec v = bot.get_vec("chanops.protect");
		bug_var(v.size());
		for(const str& s: v)
		{
			bug_var(s);
			str chan_preg, who;
			siss(s) >> chan_preg >> who;
			bug_var(chan_preg);
			bug_var(who);
			if(bot.preg_match(chan_preg, chan, true) && bot.wild_match(user, who))
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
	}

	return true;
}

bool ChanopsIrcBotPlugin::kick_event(const message& msg)
{
	BUG_COMMAND(msg);
	//---------------------------------------------------
	//                  line: :SooKee!~SooKee@SooKee.users.quakenet.org KICK #skivvy Skivvy :Skivvy
	//                prefix: SooKee!~SooKee@SooKee.users.quakenet.org
	//               command: KICK
	//                params:  #skivvy Skivvy :Skivvy
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : ~SooKee
	// get_host()           : SooKee.users.quakenet.org
	// param                : #skivvy
	// param                : Skivvy
	// param                : Skivvy
	// middle               : #skivvy
	// middle               : Skivvy
	// trailing             : Skivvy
	// get_nick()           : SooKee
	// get_chan()           : #skivvy
	// get_user_cmd()       : Skivvy
	// get_user_params()    :
	//---------------------------------------------------

	str_vec params = msg.get_params();
	if(params.size() < 2)
	{
		log("chanops: BAD KICK EVENT: " << msg.line);
		return false;
	}

	str chan = params[0];
	str who = params[1];

	if(who == bot.nick && bot.get("chanops.kick.rejoin", false))
		irc->join(chan);

	str_vec responses = bot.get_vec("chanops.kick.response");
	if(responses.empty())
		return true;

	uns idx = rand_int(0, responses.size() - 1);
	bug_var(idx);

	if(idx >= responses.size())
		return false;

	str response = responses[idx];
	bug_var(response);

//	for(siz i = 0; i < response.size(); ++i)
//		if(response[i] == '*')
//			if(!i || response[i - 1] != '\\')
//				{ response.replace(i, 1, who); i += who.size() - 1; }
//			else if(i && response[i - 1] == '\\')
//				response.replace(i - 1, 1, "");

	response = wild_replace(response, who);
	return irc->me(msg.get_chan(), response);
}

bool ChanopsIrcBotPlugin::join_event(const message& msg)
{
	BUG_COMMAND(msg);
	//================================
	// JOIN: JOIN
	//--------------------------------
	//                  line: :SooKee!~SooKee@SooKee.users.quakenet.org JOIN #skivvy-test
	//                prefix: SooKee!~SooKee@SooKee.users.quakenet.org
	//               command: JOIN
	//                params:  #skivvy-test
	// get_servername()     :
	// get_nickname()       : SooKee
	// get_user()           : ~SooKee
	// get_host()           : SooKee.users.quakenet.org
	// param                : #skivvy-test
	// middle               : #skivvy-test
	// trailing             :
	// get_nick()           : SooKee
	// get_chan()           : #skivvy-test
	// get_user_cmd()       :
	// get_user_params()    :
	//--------------------------------

	// Auto OP/VOICE/MODE/bans etc..
	if(bot.get("server.feature.chanserv", false))
		auto_login(msg); //
	bug_var(msg.get_chan());
	bug_var(msg.get_userhost());
	bug_var(msg.get_nickname());
//	enforce_static_rules(msg.get_chan(), msg.prefix, msg.get_nickname());
//	enforce_dynamic_rules(msg.get_chan(), msg.prefix, msg.get_nickname());

	for(const str& chan: bot.get_vec(GREET_JOINERS_VEC))
	{
		if(!msg.from_channel() || msg.get_chan() != chan)
			continue;

		str_set greeted = store.get_set("greeted");

		// greet only once
		if(stl::find(greeted, msg.get_userhost()) == greeted.end()
		&& msg.get_nick() != bot.nick)
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

bool ChanopsIrcBotPlugin::enforce_dynamic_rules(const str& chan, const str& prefix, const str& nick)
{
//	bug_func();
//	bug_var(chan);
//	bug_var(prefix);
//	bug_var(nick);
//	bug_var(chanops[chan]);

	if(!chanops[chan])
		return true;

	str chan_pattern, who_pattern, why;

	bug("kicks");

	// wild kicks
	for(const str& s: store.get_vec("kick"))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->kick({chan}, {nick}, why);

	bug("bans");

	// wild bans
	why.clear();
	for(const str& s: store.get_vec("ban"))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				{ irc->mode(chan, "+b", who_pattern); irc->kick({chan}, {nick}, why); }

	str mode;

	// wild modes
	for(const str& s: store.get_vec("mode"))
		if(siss(s) >> chan_pattern >> who_pattern >> mode)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, mode , nick);

	// wild ops
	for(const str& s: store.get_vec("op"))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "+o" , nick);

	// wild voices
	for(const str& s: store.get_vec("voice"))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "+v", nick);

	return true;
}

bool ChanopsIrcBotPlugin::enforce_static_rules(const str& chan, const str& prefix, const str& nick)
{
//	bug_func();
//	bug_var(chan);
//	bug_var(prefix);
//	bug_var(nick);

	if(!chanops[chan])
		return true;

	str chan_pattern, who_pattern, why;

	// pcre kicks
	for(const str& s: bot.get_vec(CHANOPS_PCRE_KICK_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, prefix))
				irc->kick({chan}, {nick}, why);

	// wild kicks
	for(const str& s: bot.get_vec(CHANOPS_WILD_KICK_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->kick({chan}, {nick}, why);

	// wild bans
	for(const str& s: bot.get_vec(CHANOPS_WILD_BAN_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "+b", nick);

	// wild deops
	for(const str& s: bot.get_vec(CHANOPS_WILD_DEOP_VEC))
		if(sgl(siss(s) >> chan_pattern >> who_pattern, why))
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "-o", nick);

	str mode;
	// pcre modes
	for(const str& s: bot.get_vec(CHANOPS_PCRE_MODE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern >> mode)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, prefix))
				irc->mode(chan, mode , nick);

	// wild modes
	for(const str& s: bot.get_vec(CHANOPS_WILD_MODE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern >> mode)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, mode , nick);

	// pcre ops
	for(const str& s: bot.get_vec(CHANOPS_PCRE_OP_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, prefix))
				irc->mode(chan, "+o" , nick);

	// wild ops
	for(const str& s: bot.get_vec(CHANOPS_WILD_OP_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "+o" , nick);

	// pcre voices
	for(const str& s: bot.get_vec(CHANOPS_PCRE_VOICE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.preg_match(chan_pattern, chan, true) && bot.preg_match(who_pattern, prefix))
				irc->mode(chan, "+v", nick);

	// wild voices
	for(const str& s: bot.get_vec(CHANOPS_WILD_VOICE_VEC))
		if(siss(s) >> chan_pattern >> who_pattern)
			if(bot.wild_match(chan_pattern, chan, true) && bot.wild_match(who_pattern, prefix))
				irc->mode(chan, "+v", nick);

	return true;
}

}} // sookee::ircbot

