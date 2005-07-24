/* authz.c : path-based access control
 *
 * ====================================================================
 * Copyright (c) 2000-2005 CollabNet.  All rights reserved.
 *
 * This software is licensed as described in the file COPYING, which
 * you should have received as part of this distribution.  The terms
 * are also available at http://subversion.tigris.org/license-1.html.
 * If newer versions of this license are posted there, you may use a
 * newer version instead, at your option.
 *
 * This software consists of voluntary contributions made by many
 * individuals.  For exact contribution history, see the revision
 * history and logs, available at http://subversion.tigris.org/.
 * ====================================================================
 */

#include <assert.h>

#include <apr_pools.h>
#include <apr_file_io.h>

#include "svn_pools.h"
#include "svn_error.h"
#include "svn_path.h"
#include "svn_repos.h"
#include "svn_config.h"


struct authz_lookup_baton {
  apr_pool_t *pool;
  svn_config_t *config;

  const char *user;
  svn_repos_authz_access_t allow;
  svn_repos_authz_access_t deny;

  svn_repos_authz_access_t required_access;
  const char *repos_path;
  const char *qualified_repos_path;

  svn_boolean_t access;
};

struct authz_validate_baton {
  apr_pool_t *pool;
  svn_config_t *config;
  svn_error_t *err;
};


/* The data type storing the internal representation of an authz
 * configuration.  Currently this is just a wrapper around a
 * svn_config_t. */
struct svn_authz_t
{
  svn_config_t *cfg;
};



/* Determine whether the required access is granted given what authz
 * are allowed and denied.  Return TRUE if the required access is
 * granted.
 *
 * Access is granted either when no required access is explicitely
 * denied (implicit grant), or when the required access is explicitely
 * granted, overriding any denials.
 */
static svn_boolean_t
authz_access_is_granted (svn_repos_authz_access_t allow,
                         svn_repos_authz_access_t deny,
                         svn_repos_authz_access_t required)
{
  svn_repos_authz_access_t stripped_req =
    required & (svn_authz_read | svn_authz_write);

  if ((deny & required) == svn_authz_none)
    return TRUE;
  else if ((allow & required) == stripped_req)
    return TRUE;
  else
    return FALSE;
}



/* Decide whether the required access has been conclusively
 * determined.  Return TRUE if the given allow/deny authz are
 * conclusive regarding the required authz.
 *
 * Conclusive determination occurs when any of the required authz are
 * granted or denied by allow/deny.
 */
static svn_boolean_t
authz_access_is_determined (svn_repos_authz_access_t allow,
                            svn_repos_authz_access_t deny,
                            svn_repos_authz_access_t required)
{
  if ((deny & required) || (allow & required))
    return TRUE;
  else
    return FALSE;
}


static svn_boolean_t
authz_group_contains_user (svn_config_t *cfg,
                           const char *group,
                           const char *user,
                           apr_pool_t *pool)
{
  const char *value;
  apr_array_header_t *list;
  int i;

  svn_config_get (cfg, &value, "groups", group, NULL);

  list = svn_cstring_split (value, ",", TRUE, pool);

  for (i = 0; i < list->nelts; i++)
    {
      const char *group_user = APR_ARRAY_IDX(list, i, char *);

      /* If the 'user' is a subgroup, recurse into it. */
      if (*group_user == '@')
        {
          if (authz_group_contains_user (cfg, &group_user[1],
                                         user, pool))
            return TRUE;
        }

      /* If the user matches, stop. */
      else if (strcmp (user, group_user) == 0)
        return TRUE;
    }

  return FALSE;
}



/* Callback to parse one line of an authz file and update the
 * authz_baton accordingly.
 */
static svn_boolean_t
authz_parse_line (const char *name, const char *value, void *baton)
{
  struct authz_lookup_baton *b = baton;

  /* Work out whether this ACL line applies to the user. */
  if (strcmp (name, "*") != 0)
    {
      /* Non-anon rule, anon user.  Stop. */
      if (!b->user)
        return TRUE;

      /* Group rule and user not in group.  Stop. */
      if (*name == '@')
        {
          if (!authz_group_contains_user (b->config, &name[1],
                                          b->user, b->pool))
            return TRUE;
        }

      /* User rule for wrong user.  Stop. */
      else if (strcmp (name, b->user) != 0)
        return TRUE;
    }

  /* Set the access grants for the rule. */
  if (strchr (value, 'r'))
    b->allow |= svn_authz_read;
  else
    b->deny |= svn_authz_read;

  if (strchr (value, 'w'))
    b->allow |= svn_authz_write;
  else
    b->deny |= svn_authz_write;

  return TRUE;
}



/* Callback to parse a section and update the authz_baton if the
 * section denies access to the subtree the baton describes.
 */
static svn_boolean_t
authz_parse_section (const char *section_name, void *baton)
{
  struct authz_lookup_baton *b = baton;
  svn_boolean_t conclusive;

  /* Does the section apply to us? */
  if (strncmp (section_name, b->qualified_repos_path,
               strlen (b->qualified_repos_path)) != 0
      && strncmp (section_name, b->repos_path,
                  strlen (b->repos_path)) != 0)
    return TRUE;

  /* Work out what this section grants. */
  b->allow = b->deny = 0;
  svn_config_enumerate (b->config, section_name,
                        authz_parse_line, b);

  /* Has the section explicitely determined an access? */
  conclusive = authz_access_is_determined (b->allow, b->deny,
                                           b->required_access);

  /* Is access granted OR inconclusive? */
  b->access = authz_access_is_granted (b->allow, b->deny,
                                       b->required_access)
    || !conclusive;

  /* As long as access isn't conclusively denied, carry on. */
  return b->access;
}



/* Validate access to the given user for the given path.  This
 * function checks rules for exactly the given path, and first tries
 * to access a section specific to the given repository before falling
 * back to pan-repository rules.
 *
 * Update *access_granted to inform the caller of the outcome of the
 * lookup.  Return a boolean indicating whether the access rights were
 * successfully determined.
 */
static svn_boolean_t
authz_get_path_access (svn_config_t *cfg, const char *repos_name,
                       const char *path, const char *user,
                       svn_repos_authz_access_t required_access,
                       svn_boolean_t *access_granted,
                       apr_pool_t *pool)
{
  const char *qualified_path;
  struct authz_lookup_baton baton = { 0 };

  baton.pool = pool;
  baton.config = cfg;
  baton.user = user;

  /* Try to locate a repository-specific block first. */
  qualified_path = apr_pstrcat (pool, repos_name, ":", path, NULL);
  svn_config_enumerate (cfg, qualified_path,
                        authz_parse_line, &baton);

  *access_granted = authz_access_is_granted (baton.allow, baton.deny,
                                             required_access);

  /* If the first test has determined access, stop now. */
  if (authz_access_is_determined (baton.allow, baton.deny,
                                  required_access))
    return TRUE;

  /* No repository specific rule, try pan-repository rules. */
  svn_config_enumerate (cfg, path, authz_parse_line, &baton);

  *access_granted = authz_access_is_granted (baton.allow, baton.deny,
                                             required_access);
  return authz_access_is_determined (baton.allow, baton.deny,
                                     required_access);
}



/* Validate access to the given user for the subtree starting at the
 * given path.  This function walks the whole authz file in search of
 * rules applying to paths in the requested subtree which deny the
 * requested access.
 *
 * As soon as one is found, or else when the whole ACL file has been
 * searched, return the updated authorization status.
 */
static svn_boolean_t
authz_get_tree_access (svn_config_t *cfg, const char *repos_name,
                       const char *path, const char *user,
                       svn_repos_authz_access_t required_access,
                       apr_pool_t *pool)
{
  struct authz_lookup_baton baton = { 0 };

  baton.pool = pool;
  baton.config = cfg;
  baton.user = user;
  baton.required_access = required_access;
  baton.repos_path = path;
  baton.qualified_repos_path = apr_pstrcat (pool, repos_name,
                                            ":", path, NULL);
  /* Default to access granted if no rules say otherwise. */
  baton.access = TRUE;

  svn_config_enumerate_sections (cfg, authz_parse_section, &baton);

  return baton.access;
}



/* Examine the entire definition of a group, checking for errors.  The
 * errors detected are references to non-existent groups and circular
 * dependencies between groups. */
static svn_error_t *
authz_group_walk (svn_config_t *cfg,
                  const char *group,
                  apr_hash_t *checked_groups,
                  apr_pool_t *pool)
{
  const char *value;
  apr_array_header_t *list;
  int i;

  svn_config_get (cfg, &value, "groups", group, NULL);
  /* Having a non-existent group in the ACL configuration might be the
     sign of a typo.  Refuse to perform authz on uncertain rules. */
  if (!value)
    return svn_error_createf (SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                              "An authz rule refers to group '%s', "
                              "which is undefined",
                              group);

  list = svn_cstring_split (value, ",", TRUE, pool);

  for (i = 0; i < list->nelts; i++)
    {
      const char *group_user = APR_ARRAY_IDX(list, i, char *);

      /* If the 'user' is a subgroup, recurse into it. */
      if (*group_user == '@')
        {
          /* A circular dependency between groups is a Bad Thing.  We
             don't do authz with invalid ACL files. */
          if (apr_hash_get (checked_groups, &group_user[1],
                            APR_HASH_KEY_STRING))
            return svn_error_createf (SVN_ERR_AUTHZ_INVALID_CONFIG,
                                      NULL,
                                      "Circular dependency between "
                                      "groups '%s' and '%s'",
                                      &group_user[1], group);

          /* Add group to hash of checked groups. */
          apr_hash_set (checked_groups, &group_user[1],
                        APR_HASH_KEY_STRING, "");

          /* Recurse on that group. */
          SVN_ERR (authz_group_walk (cfg, &group_user[1],
                                     checked_groups, pool));
        }
    }

  return SVN_NO_ERROR;
}



/* Callback to check whether groups mentioned in the given authz rule
   exist. */
static svn_boolean_t authz_validate_rule (const char *name,
                                          const char *value,
                                          void *baton)
{
  const char *val;
  struct authz_validate_baton *b = baton;

  /* If the rule applies to a group, check its existence. */
  if (*name == '@')
    {
      svn_config_get (b->config, &val, "groups",
                      &name[1], NULL);
      /* Having a non-existent group in the ACL configuration might be
         the sign of a typo.  Refuse to perform authz on uncertain
         rules. */
      if (!val)
        {
          b->err = svn_error_createf (SVN_ERR_AUTHZ_INVALID_CONFIG, NULL,
                                      "An authz rule refers to group "
                                      "'%s', which is undefined",
                                      name);
          return FALSE;
        }
    }

  return TRUE;
}



/* Callback to check whether groups defined don't cyclicly depend on
   each other. */
static svn_boolean_t authz_validate_group (const char *name,
                                           const char *value,
                                           void *baton)
{
  struct authz_validate_baton *b = baton;

  b->err = authz_group_walk (b->config, name, apr_hash_make (b->pool),
                             b->pool);
  if (b->err)
    return FALSE;

  return TRUE;
}



/* Callback to validate a section of the authz ruleset. */
static svn_boolean_t authz_validate_section (const char *name,
                                             void *baton)
{
  struct authz_validate_baton *b = baton;

  /* If the section is the groups definition, use the group checking
     callback. Otherwise, use the rule checking callback. */
  if (strncmp (name, "groups", 6) == 0)
    svn_config_enumerate (b->config, name, authz_validate_group, baton);
  else
    svn_config_enumerate (b->config, name, authz_validate_rule, baton);

  if (b->err)
    return FALSE;

  return TRUE;
}



svn_error_t *
svn_repos_authz_read (svn_authz_t **authzp, const char *file,
                      svn_boolean_t must_exist, apr_pool_t *pool)
{
  svn_authz_t *authz = apr_palloc (pool, sizeof(*authz));
  struct authz_validate_baton baton = { 0 };

  baton.pool = pool;
  baton.err = SVN_NO_ERROR;

  /* Load the rule file. */
  SVN_ERR (svn_config_read (&authz->cfg, file, must_exist, pool));
  baton.config = authz->cfg;

  /* Step through the entire rule file, aborting on error. */
  svn_config_enumerate_sections (authz->cfg, authz_validate_section,
                                 &baton);
  SVN_ERR (baton.err);

  *authzp = authz;
  return SVN_NO_ERROR;
}



svn_error_t *
svn_repos_authz_check_access (svn_authz_t *authz, const char *repos_name,
                              const char *path, const char *user,
                              svn_repos_authz_access_t required_access,
                              svn_boolean_t *access_granted,
                              apr_pool_t *pool)
{
  const char *base_name = path;
  const char *current_path = path;

  /* Determine the granted access for the requested path. */
  while (!authz_get_path_access (authz->cfg, repos_name,
                                 current_path, user,
                                 required_access,
                                 access_granted,
                                 pool))
    {
      /* Stop if the loop hits the repository root with no
         results. */
      if (base_name[0] == '/' && base_name[1] == '\0')
        {
          /* Deny access by default. */
          *access_granted = FALSE;
          return SVN_NO_ERROR;
        }

      /* Work back to the parent path. */
      svn_path_split (current_path, &current_path, &base_name, pool);
    }

  /* If the caller requested recursive access, we need to walk through
     the entire authz config to see whether any child paths are denied
     to the requested user. */
  if (*access_granted && (required_access & svn_authz_recursive))
    *access_granted = authz_get_tree_access (authz->cfg, repos_name, path,
                                             user, required_access, pool);

  return SVN_NO_ERROR;
}
