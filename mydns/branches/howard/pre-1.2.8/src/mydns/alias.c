/**************************************************************************************************
	$Id: alias.c,v 1.15 2006/01/18 20:46:46 bboy Exp $

	Copyright (C) 2002-2005  Don Moore <bboy@bboy.net>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at Your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**************************************************************************************************/

#include "named.h"

/* Make this nonzero to enable debugging for this source file */
#define	DEBUG_ALIAS	1


#if ALIAS_ENABLED
/**************************************************************************************************
	FIND_ALIAS
	Find an ALIAS or A record for the alias.
	Returns the RR or NULL if not found.
**************************************************************************************************/
MYDNS_RR *
find_alias(TASK *t, char *fqdn) {
  register MYDNS_SOA *soa;
  register MYDNS_RR *rr;
  register char *label;
  char *name = NULL;
	
  /* Load the SOA for the alias name. */

  if (!(soa = find_soa2(t, fqdn, &name)))
    return (NULL);

  /* Examine each label in the name, one at a time; look for relevant records */
  for (label = name; ; label++) {
    if (label == name || *label == '.') {
      if (label[0] == '.' && label[1]) label++;		/* Advance past leading dot */
#if DEBUG_ENABLED && DEBUG_ALIAS
      Debug("%s: label=`%s'", desctask(t), label);
#endif

      /* Do an exact match if the label is the first in the list */
      if (label == name) {
#if DEBUG_ENABLED && DEBUG_ALIAS
	Debug("%s: trying exact match `%s'", desctask(t), label);
#endif
	if ((rr = find_rr(t, soa, DNS_QTYPE_A, label))) {
	  RELEASE(name);
	  return (rr);
	}
      }

      /* No exact match. If the label isn't empty, replace the first part
	 of the label with `*' and check for wildcard matches. */
      if (*label) {
	uchar *wclabel = NULL, *c;

	/* Generate wildcarded label, i.e. `*.example' or maybe just `*'. */
	if (!(c = strchr(label, '.')))
	  wclabel = STRDUP("*");
	else
	  ASPRINTF(&wclabel, "*.%s", c);

#if DEBUG_ENABLED && DEBUG_ALIAS
	Debug("%s: trying wildcard `%s'", desctask(t), wclabel);
#endif
	if ((rr = find_rr(t, soa, DNS_QTYPE_A, wclabel))) {
	  RELEASE(name);
	  RELEASE(wclabel);
	  return (rr);
	}
	RELEASE(wclabel);
      }
      if (!*label)
	break;
    }
    RELEASE(name);
    return (NULL);
}
/*--- find_alias() ------------------------------------------------------------------------------*/


/**************************************************************************************************
	ALIAS_RECURSE
	If the task has a matching ALIAS record, recurse into it.
	Returns the number of records added.
**************************************************************************************************/
int
alias_recurse(TASK *t, datasection_t section, char *fqdn, MYDNS_SOA *soa, char *label, MYDNS_RR *alias) {
  uint32_t		aliases[MAX_ALIAS_LEVEL];
  char			*name = NULL;
  register MYDNS_RR	*rr;
  register int		depth, n;

  if (LASTCHAR(alias->data) != '.')
    ASPRINTF(&name, "%s.%s", alias->data, soa->origin);
  else
    name = STRDUP(alias->data);

  for (depth = 0; depth < MAX_ALIAS_LEVEL; depth++) {
#if DEBUG_ENABLED && DEBUG_ALIAS
    Debug("%s: ALIAS -> `%s'", desctask(t), name);
#endif
    /* Are there any alias records? */
    if ((rr = find_alias(t, name))) {
      /* We need an A record that is not an alias to end the chain. */
      if (rr->alias == 0) {
	/*
	** Override the id and name, because rrlist_add() checks for
	** duplicates and we might have several records aliased to one
	*/
	rr->id = alias->id;
	strcpy(rr->name, alias->name);
	rrlist_add(t, section, DNS_RRTYPE_RR, (void *)rr, fqdn);
	t->sort_level++;
	mydns_rr_free(rr);
	RELEASE(name);
	return (1);
      }

      /* Append origin if needed */
      if (MYDNS_RR_DATA_LENGTH(rr) > 0 && LASTCHAR(MYDNS_RR_DATA_VALUE(rr)) != '.') {
	mydns_rr_append_origin(rr, origin);
      }

      /* Check aliases list; if we are looping, stop. Otherwise add this to the list. */
      for (n = 0; n < depth; n++)
	if (aliases[n] == rr->id) {
	  /* ALIAS loop: We aren't going to find an A record, so we're done. */
	  Verbose("%s: %s: %s (depth %d)", desctask(t), _("ALIAS loop detected"), fqdn, depth);
	  mydns_rr_free(rr);
	  RELEASE(name);
	  return (0);
	}
      aliases[depth] = rr->id;

      /* Continue search with new alias. */
      strncpy(name, rr->data, sizeof(name)-1);
      mydns_rr_free(rr);
    } else {
      Verbose("%s: %s: %s -> %s", desctask(t), _("ALIAS chain is broken"), fqdn, name);
      RELEASE(name);
      return (0);
    }
  }
  Verbose("%s: %s: %s -> %s (depth %d)", desctask(t), _("max ALIAS depth exceeded"), fqdn, alias->data, depth);
  RELEASE(name);
  return (0);
}
/*--- alias_recurse() ---------------------------------------------------------------------------*/

#endif /* ALIAS_ENABLED */

/* vi:set ts=3: */
