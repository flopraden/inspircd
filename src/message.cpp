/*       +------------------------------------+
 *       | Inspire Internet Relay Chat Daemon |
 *       +------------------------------------+
 *
 *  Inspire is copyright (C) 2002-2004 ChatSpike-Dev.
 *                       E-mail:
 *                <brain@chatspike.net>
 *           	  <Craig@chatspike.net>
 *     
 * Written by Craig Edwards, Craig McLure, and others.
 * This program is free but copyrighted software; see
 *            the file COPYING for details.
 *
 * ---------------------------------------------------
 */

using namespace std;

#include "inspircd_config.h"
#include "inspircd.h"
#include "inspircd_io.h"
#include "inspircd_util.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/errno.h>
#include <sys/utsname.h>
#include <time.h>
#include <string>
#ifdef GCC3
#include <ext/hash_map>
#else
#include <hash_map>
#endif
#include <map>
#include <sstream>
#include <vector>
#include <deque>
#include "users.h"
#include "ctables.h"
#include "globals.h"
#include "modules.h"
#include "dynamic.h"
#include "wildcard.h"
#include "commands.h"
#include "message.h"
#include "inspstring.h"
#include "dns.h"
#include "helperfuncs.h"

extern int MODCOUNT;
extern std::vector<Module*> modules;
extern std::vector<ircd_module*> factory;

extern char ServerName[MAXBUF];

extern time_t TIME;

extern FILE *log_file;
extern char DNSServer[MAXBUF];

/* return 0 or 1 depending if users u and u2 share one or more common channels
 * (used by QUIT, NICK etc which arent channel specific notices) */

int common_channels(userrec *u, userrec *u2)
{
	if ((!u) || (!u2))
	{
		log(DEFAULT,"*** BUG *** common_channels was given an invalid parameter");
		return 0;
	}
	for (unsigned int i = 0; i < u->chans.size(); i++)
	{
		for (unsigned int z = 0; z != u2->chans.size(); z++)
		{
			if ((u->chans[i].channel != NULL) && (u2->chans[z].channel != NULL))
			{
				if ((!strcasecmp(u->chans[i].channel->name,u2->chans[z].channel->name)) && (u->chans[i].channel) && (u2->chans[z].channel) && (u->registered == 7) && (u2->registered == 7))
				{
					if ((c_count(u)) && (c_count(u2)))
					{
						return 1;
					}
				}
			}
		}
	}
	return 0;
}


void tidystring(char* str)
{
	// strips out double spaces before a : parameter
	
	char temp[MAXBUF];
	bool go_again = true;
	
	if (!str)
	{
		return;
	}
	
	while ((str[0] == ' ') && (strlen(str)>0))
	{
		str++;
	}
	
	while (go_again)
	{
		bool noparse = false;
		unsigned int t = 0, a = 0;
		go_again = false;
		while (a < strlen(str))
		{
			if ((a<strlen(str)-1) && (noparse==false))
			{
				if ((str[a] == ' ') && (str[a+1] == ' '))
				{
					log(DEBUG,"Tidied extra space out of string: %s",str);
					go_again = true;
					a++;
				}
			}
			
			if (a<strlen(str)-1)
			{
				if ((str[a] == ' ') && (str[a+1] == ':'))
				{
					noparse = true;
				}
			}
			
			temp[t++] = str[a++];
		}
		temp[t] = '\0';
		strlcpy(str,temp,MAXBUF);
	}
}

/* chop a string down to 512 characters and preserve linefeed (irc max
 * line length) */

void chop(char* str)
{
	if (!str)
	{
		log(DEBUG,"ERROR! Null string passed to chop()!");
		return;
	}
	string temp = str;
	FOREACH_MOD OnServerRaw(temp,false,NULL);
	const char* str2 = temp.c_str();
	snprintf(str,MAXBUF,"%s",str2);
	if (strlen(str) >= 511)
	{
		str[510] = '\r';
		str[511] = '\n';
		str[512] = '\0';
		log(DEBUG,"Excess line chopped.");
	}
}


void Blocking(int s)
{
	int flags;
	log(DEBUG,"Blocking: %d",s);
	flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, flags ^ O_NONBLOCK);
}

void NonBlocking(int s)
{
	int flags;
	log(DEBUG,"NonBlocking: %d",s);
	flags = fcntl(s, F_GETFL, 0);
	fcntl(s, F_SETFL, flags | O_NONBLOCK);
}

int CleanAndResolve (char *resolvedHost, const char *unresolvedHost)
{
	DNS d(DNSServer);
	int fd = d.ReverseLookup(unresolvedHost);
	if (fd < 1)
		return 0;
	time_t T = time(NULL)+1;
	while ((!d.HasResult()) && (time(NULL)<T));
	std::string ipaddr = d.GetResult();
	strlcpy(resolvedHost,ipaddr.c_str(),MAXBUF);
	return (ipaddr != "");
}

int c_count(userrec* u)
{
	int z = 0;
	for (unsigned int i =0; i < u->chans.size(); i++)
		if (u->chans[i].channel != NULL)
			z++;
	return z;

}

bool hasumode(userrec* user, char mode)
{
	if (user)
	{
		return (strchr(user->modes,mode)>0);
	}
	else return false;
}


void ChangeName(userrec* user, const char* gecos)
{
	if (!strcasecmp(user->server,ServerName))
	{
		int MOD_RESULT = 0;
		FOREACH_RESULT(OnChangeLocalUserGECOS(user,gecos));
		if (MOD_RESULT)
			return;
		FOREACH_MOD OnChangeName(user,gecos);
	}
	strlcpy(user->fullname,gecos,MAXBUF);
}

void ChangeDisplayedHost(userrec* user, const char* host)
{
        if (!strcasecmp(user->server,ServerName))
        {
                int MOD_RESULT = 0;
                FOREACH_RESULT(OnChangeLocalUserHost(user,host));
                if (MOD_RESULT)
                        return;
		FOREACH_MOD OnChangeHost(user,host);
        }
	strlcpy(user->dhost,host,160);
}

/* verify that a user's ident and nickname is valid */

int isident(const char* n)
{
        if (!n)

        {
                return 0;
        }
        if (!strcmp(n,""))
        {
                return 0;
        }
        for (unsigned int i = 0; i < strlen(n); i++)
        {
                if ((n[i] < 33) || (n[i] > 125))
                {
                        return 0;
                }
                /* can't occur ANYWHERE in an Ident! */
                if (strchr("<>,/?:;@'~#=+()*&%$� \"!",n[i]))
                {
                        return 0;
                }
        }
        return 1;
}


int isnick(const char* n)
{
	if (!n)
	{
		return 0;
	}
	if (!strcmp(n,""))
	{
		return 0;
	}
	if (strlen(n) > NICKMAX)
	{
		return 0;
	}
	for (unsigned int i = 0; i != strlen(n); i++)
	{
		if ((n[i] < 33) || (n[i] > 125))
		{
			return 0;
		}
		/* can't occur ANYWHERE in a nickname! */
		if (strchr("<>,./?:;@'~#=+()*&%$� \"!",n[i]))
		{
			return 0;
		}
		/* can't occur as the first char of a nickname... */
		if ((strchr("0123456789",n[i])) && (!i))
		{
			return 0;
		}
	}
	return 1;
}

/* returns the status character for a given user on a channel, e.g. @ for op,
 * % for halfop etc. If the user has several modes set, the highest mode
 * the user has must be returned. */

char* cmode(userrec *user, chanrec *chan)
{
	if ((!user) || (!chan))
	{
		log(DEFAULT,"*** BUG *** cmode was given an invalid parameter");
		return "";
	}

	for (unsigned int i = 0; i < user->chans.size(); i++)
	{
		if (user->chans[i].channel)
		{
			if ((!strcasecmp(user->chans[i].channel->name,chan->name)) && (chan != NULL))
			{
				if ((user->chans[i].uc_modes & UCMODE_OP) > 0)
				{
					return "@";
				}
				if ((user->chans[i].uc_modes & UCMODE_HOP) > 0)
				{
					return "%";
				}
				if ((user->chans[i].uc_modes & UCMODE_VOICE) > 0)
				{
					return "+";
				}
				return "";
			}
		}
	}
	return "";
}

/* returns the status value for a given user on a channel, e.g. STATUS_OP for
 * op, STATUS_VOICE for voice etc. If the user has several modes set, the
 * highest mode the user has must be returned. */

int cstatus(userrec *user, chanrec *chan)
{
	if ((!chan) || (!user))
	{
		log(DEFAULT,"*** BUG *** cstatus was given an invalid parameter");
		return 0;
	}

	if (is_uline(user->server))
		return STATUS_OP;

	for (unsigned int i = 0; i < user->chans.size(); i++)
	{
		if (user->chans[i].channel)
		{
			if ((!strcasecmp(user->chans[i].channel->name,chan->name)) && (chan != NULL))
			{
				if ((user->chans[i].uc_modes & UCMODE_OP) > 0)
				{
					return STATUS_OP;
				}
				if ((user->chans[i].uc_modes & UCMODE_HOP) > 0)
				{
					return STATUS_HOP;
				}
				if ((user->chans[i].uc_modes & UCMODE_VOICE) > 0)
				{
					return STATUS_VOICE;
				}
				return STATUS_NORMAL;
			}
		}
	}
	return STATUS_NORMAL;
}

/* returns 1 if user u has channel c in their record, 0 if not */

int has_channel(userrec *u, chanrec *c)
{
	if ((!u) || (!c))
	{
		log(DEFAULT,"*** BUG *** has_channel was given an invalid parameter");
		return 0;
	}
	for (unsigned int i =0; i < u->chans.size(); i++)
	{
		if (u->chans[i].channel)
		{
			if (!strcasecmp(u->chans[i].channel->name,c->name))
			{
				return 1;
			}
		}
	}
	return 0;
}


void TidyBan(char *ban)
{
	if (!ban) {
		log(DEFAULT,"*** BUG *** TidyBan was given an invalid parameter");
		return;
	}
	
	char temp[MAXBUF],NICK[MAXBUF],IDENT[MAXBUF],HOST[MAXBUF];

	strlcpy(temp,ban,MAXBUF);

	char* pos_of_pling = strchr(temp,'!');
	char* pos_of_at = strchr(temp,'@');

	pos_of_pling[0] = '\0';
	pos_of_at[0] = '\0';
	pos_of_pling++;
	pos_of_at++;

	strlcpy(NICK,temp,NICKMAX);
	strlcpy(IDENT,pos_of_pling,IDENTMAX+1);
	strlcpy(HOST,pos_of_at,160);

	snprintf(ban,MAXBUF,"%s!%s@%s",NICK,IDENT,HOST);
}

char lst[MAXBUF];

char* chlist(userrec *user,userrec* source)
{
	char cmp[MAXBUF];
        log(DEBUG,"chlist: %s",user->nick);
	strcpy(lst,"");
	if (!user)
	{
		return lst;
	}
	for (unsigned int i = 0; i < user->chans.size(); i++)
	{
		if (user->chans[i].channel != NULL)
		{
			if (user->chans[i].channel->name)
			{
				strlcpy(cmp,user->chans[i].channel->name,MAXBUF);
				strlcat(cmp," ",MAXBUF);
				if (!strstr(lst,cmp))
				{
					// if the channel is NOT private/secret, OR the source user is on the channel
					if (((!(user->chans[i].channel->binarymodes & CM_PRIVATE)) && (!(user->chans[i].channel->binarymodes & CM_SECRET))) || (has_channel(source,user->chans[i].channel)))
					{
						strlcat(lst,cmode(user,user->chans[i].channel),MAXBUF);
						strlcat(lst,user->chans[i].channel->name,MAXBUF);
						strlcat(lst," ",MAXBUF);
					}
				}
			}
		}
	}
	if (strlen(lst))
	{
		lst[strlen(lst)-1] = '\0'; // chop trailing space
	}
	return lst;
}



