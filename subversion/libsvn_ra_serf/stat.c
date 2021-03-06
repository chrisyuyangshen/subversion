/*
 * stat.c :  file and directory stat and read functions
 *
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 */



#define APR_WANT_STRFUNC
#include <apr_want.h>

#include <serf.h>

#include "svn_private_config.h"
#include "svn_pools.h"
#include "svn_xml.h"
#include "../libsvn_ra/ra_loader.h"
#include "svn_config.h"
#include "svn_dirent_uri.h"
#include "svn_hash.h"
#include "svn_path.h"
#include "svn_props.h"
#include "svn_time.h"
#include "svn_version.h"

#include "private/svn_dav_protocol.h"
#include "private/svn_dep_compat.h"
#include "private/svn_fspath.h"

#include "ra_serf.h"



static svn_error_t *
fetch_path_props(apr_hash_t **props,
                 svn_ra_serf__session_t *session,
                 const char *session_relpath,
                 svn_revnum_t revision,
                 const svn_ra_serf__dav_props_t *desired_props,
                 apr_pool_t *result_pool,
                 apr_pool_t *scratch_pool)
{
  const char *url;

  url = session->session_url.path;

  /* If we have a relative path, append it. */
  if (session_relpath)
    url = svn_path_url_add_component2(url, session_relpath, scratch_pool);

  /* If we were given a specific revision, get a URL that refers to that
     specific revision (rather than floating with HEAD).  */
  if (SVN_IS_VALID_REVNUM(revision))
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&url, NULL /* latest_revnum */,
                                          session, NULL /* conn */,
                                          url, revision,
                                          scratch_pool, scratch_pool));
    }

  /* URL is stable, so we use SVN_INVALID_REVNUM since it is now irrelevant.
     Or we started with SVN_INVALID_REVNUM and URL may be floating.  */
  SVN_ERR(svn_ra_serf__fetch_node_props(props, session->conns[0],
                                        url, SVN_INVALID_REVNUM,
                                        desired_props,
                                        result_pool, scratch_pool));

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.check_path(). */
svn_error_t *
svn_ra_serf__check_path(svn_ra_session_t *ra_session,
                        const char *rel_path,
                        svn_revnum_t revision,
                        svn_node_kind_t *kind,
                        apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;

  svn_error_t *err = fetch_path_props(&props, session, rel_path,
                                      revision, check_path_props,
                                      pool, pool);

  if (err && err->apr_err == SVN_ERR_FS_NOT_FOUND)
    {
      svn_error_clear(err);
      *kind = svn_node_none;
    }
  else
    {
      /* Any other error, raise to caller. */
      if (err)
        return svn_error_trace(err);

      SVN_ERR(svn_ra_serf__get_resource_type(kind, props));
    }

  return SVN_NO_ERROR;
}


struct dirent_walker_baton_t {
  /* Update the fields in this entry.  */
  svn_dirent_t *entry;

  svn_tristate_t *supports_deadprop_count;

  /* If allocations are necessary, then use this pool.  */
  apr_pool_t *result_pool;
};

static svn_error_t *
dirent_walker(void *baton,
              const char *ns,
              const char *name,
              const svn_string_t *val,
              apr_pool_t *scratch_pool)
{
  struct dirent_walker_baton_t *dwb = baton;

  if (strcmp(ns, SVN_DAV_PROP_NS_CUSTOM) == 0)
    {
      dwb->entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_SVN) == 0)
    {
      dwb->entry->has_props = TRUE;
    }
  else if (strcmp(ns, SVN_DAV_PROP_NS_DAV) == 0)
    {
      if(strcmp(name, "deadprop-count") == 0)
        {
          if (*val->data)
            {
              apr_int64_t deadprop_count;
              SVN_ERR(svn_cstring_atoi64(&deadprop_count, val->data));
              dwb->entry->has_props = deadprop_count > 0;
              if (dwb->supports_deadprop_count)
                *dwb->supports_deadprop_count = svn_tristate_true;
            }
          else if (dwb->supports_deadprop_count)
            *dwb->supports_deadprop_count = svn_tristate_false;
        }
    }
  else if (strcmp(ns, "DAV:") == 0)
    {
      if (strcmp(name, SVN_DAV__VERSION_NAME) == 0)
        {
          apr_int64_t rev;
          SVN_ERR(svn_cstring_atoi64(&rev, val->data));

          dwb->entry->created_rev = (svn_revnum_t)rev;
        }
      else if (strcmp(name, "creator-displayname") == 0)
        {
          dwb->entry->last_author = val->data;
        }
      else if (strcmp(name, SVN_DAV__CREATIONDATE) == 0)
        {
          SVN_ERR(svn_time_from_cstring(&dwb->entry->time,
                                        val->data,
                                        dwb->result_pool));
        }
      else if (strcmp(name, "getcontentlength") == 0)
        {
          /* 'getcontentlength' property is empty for directories. */
          if (val->len)
            {
              SVN_ERR(svn_cstring_atoi64(&dwb->entry->size, val->data));
            }
        }
      else if (strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(val->data, "collection") == 0)
            {
              dwb->entry->kind = svn_node_dir;
            }
          else
            {
              dwb->entry->kind = svn_node_file;
            }
        }
    }

  return SVN_NO_ERROR;
}

static const svn_ra_serf__dav_props_t *
get_dirent_props(apr_uint32_t dirent_fields,
                 svn_ra_serf__session_t *session,
                 apr_pool_t *pool)
{
  svn_ra_serf__dav_props_t *prop;
  apr_array_header_t *props = apr_array_make
    (pool, 7, sizeof(svn_ra_serf__dav_props_t));

  if (session->supports_deadprop_count != svn_tristate_false
      || ! (dirent_fields & SVN_DIRENT_HAS_PROPS))
    {
      if (dirent_fields & SVN_DIRENT_KIND)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "resourcetype";
        }

      if (dirent_fields & SVN_DIRENT_SIZE)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "getcontentlength";
        }

      if (dirent_fields & SVN_DIRENT_HAS_PROPS)
        {
          prop = apr_array_push(props);
          prop->xmlns = SVN_DAV_PROP_NS_DAV;
          prop->name = "deadprop-count";
        }

      if (dirent_fields & SVN_DIRENT_CREATED_REV)
        {
          svn_ra_serf__dav_props_t *p = apr_array_push(props);
          p->xmlns = "DAV:";
          p->name = SVN_DAV__VERSION_NAME;
        }

      if (dirent_fields & SVN_DIRENT_TIME)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = SVN_DAV__CREATIONDATE;
        }

      if (dirent_fields & SVN_DIRENT_LAST_AUTHOR)
        {
          prop = apr_array_push(props);
          prop->xmlns = "DAV:";
          prop->name = "creator-displayname";
        }
    }
  else
    {
      /* We found an old subversion server that can't handle
         the deadprop-count property in the way we expect.

         The neon behavior is to retrieve all properties in this case */
      prop = apr_array_push(props);
      prop->xmlns = "DAV:";
      prop->name = "allprop";
    }

  prop = apr_array_push(props);
  prop->xmlns = NULL;
  prop->name = NULL;

  return (svn_ra_serf__dav_props_t *) props->elts;
}

/* Implements svn_ra__vtable_t.stat(). */
svn_error_t *
svn_ra_serf__stat(svn_ra_session_t *ra_session,
                  const char *rel_path,
                  svn_revnum_t revision,
                  svn_dirent_t **dirent,
                  apr_pool_t *pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_hash_t *props;
  svn_error_t *err;
  struct dirent_walker_baton_t dwb;
  svn_tristate_t deadprop_count = svn_tristate_unknown;

  err = fetch_path_props(&props,
                         session, rel_path, revision,
                         get_dirent_props(SVN_DIRENT_ALL, session, pool),
                         pool, pool);
  if (err)
    {
      if (err->apr_err == SVN_ERR_FS_NOT_FOUND)
        {
          svn_error_clear(err);
          *dirent = NULL;
          return SVN_NO_ERROR;
        }
      else
        return svn_error_trace(err);
    }

  dwb.entry = svn_dirent_create(pool);
  dwb.supports_deadprop_count = &deadprop_count;
  dwb.result_pool = pool;
  SVN_ERR(svn_ra_serf__walk_node_props(props, dirent_walker, &dwb, pool));

  if (deadprop_count == svn_tristate_false
      && session->supports_deadprop_count == svn_tristate_unknown
      && !dwb.entry->has_props)
    {
      /* We have to requery as the server didn't give us the right
         information */
      session->supports_deadprop_count = svn_tristate_false;

      SVN_ERR(fetch_path_props(&props,
                               session, rel_path, SVN_INVALID_REVNUM,
                               get_dirent_props(SVN_DIRENT_ALL, session, pool),
                               pool, pool));

      SVN_ERR(svn_ra_serf__walk_node_props(props, dirent_walker, &dwb, pool));
    }

  if (deadprop_count != svn_tristate_unknown)
    session->supports_deadprop_count = deadprop_count;

  *dirent = dwb.entry;

  return SVN_NO_ERROR;
}

/* Baton for get_dir_dirents_cb and get_dir_props_cb */
struct get_dir_baton_t
{
  apr_pool_t *result_pool;
  apr_hash_t *dirents;
  apr_hash_t *ret_props;
  svn_boolean_t is_directory;
  svn_tristate_t supports_deadprop_count;
  const char *path;
};

/* Implements svn_ra_serf__prop_func */
static svn_error_t *
get_dir_dirents_cb(void *baton,
                   const char *path,
                   const char *ns,
                   const char *name,
                   const svn_string_t *value,
                   apr_pool_t *scratch_pool)
{
  struct get_dir_baton_t *db = baton;
  const char *relpath;

  relpath = svn_fspath__skip_ancestor(db->path, path);

  if (relpath && relpath[0] != '\0')
    {
      struct dirent_walker_baton_t dwb;

      relpath = svn_path_uri_decode(relpath, scratch_pool);
      dwb.entry = svn_hash_gets(db->dirents, relpath);

      if (!dwb.entry)
        {
          dwb.entry = svn_dirent_create(db->result_pool);
          svn_hash_sets(db->dirents,
                        apr_pstrdup(db->result_pool, relpath),
                        dwb.entry);
        }

      dwb.result_pool = db->result_pool;
      dwb.supports_deadprop_count = &db->supports_deadprop_count;
      SVN_ERR(dirent_walker(&dwb, ns, name, value, scratch_pool));
    }
  else if (relpath && !db->is_directory)
    {
      if (strcmp(ns, "DAV:") == 0 && strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(value->data, "collection") != 0)
            {
              /* Tell a lie to exit early */
              return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                      _("Can't get properties of non-directory"));
            }
          else
            db->is_directory = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra_serf__prop_func */
static svn_error_t *
get_dir_props_cb(void *baton,
                 const char *path,
                 const char *ns,
                 const char *name,
                 const svn_string_t *value,
                 apr_pool_t *scratch_pool)
{
  struct get_dir_baton_t *db = baton;
  const char *propname;

  propname = svn_ra_serf__svnname_from_wirename(ns, name, db->result_pool);
  if (propname)
    {
      svn_hash_sets(db->ret_props, propname,
                    svn_string_dup(value, db->result_pool));
      return SVN_NO_ERROR;
    }

  if (!db->is_directory)
    {
      if (strcmp(ns, "DAV:") == 0 && strcmp(name, "resourcetype") == 0)
        {
          if (strcmp(value->data, "collection") != 0)
            {
              /* Tell a lie to exit early */
              return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                                      _("Can't get properties of non-directory"));
            }
          else
            db->is_directory = TRUE;
        }
    }

  return SVN_NO_ERROR;
}

/* Implements svn_ra__vtable_t.get_dir(). */
svn_error_t *
svn_ra_serf__get_dir(svn_ra_session_t *ra_session,
                     apr_hash_t **dirents,
                     svn_revnum_t *fetched_rev,
                     apr_hash_t **ret_props,
                     const char *rel_path,
                     svn_revnum_t revision,
                     apr_uint32_t dirent_fields,
                     apr_pool_t *result_pool)
{
  svn_ra_serf__session_t *session = ra_session->priv;
  apr_pool_t *scratch_pool = svn_pool_create(result_pool);
  svn_ra_serf__handler_t *dirent_handler = NULL;
  svn_ra_serf__handler_t *props_handler = NULL;
  const char *path;
  struct get_dir_baton_t gdb;

  gdb.result_pool = result_pool;
  gdb.is_directory = FALSE;
  gdb.supports_deadprop_count = svn_tristate_unknown;

  path = session->session_url.path;

  /* If we have a relative path, URI encode and append it. */
  if (rel_path)
    {
      path = svn_path_url_add_component2(path, rel_path, scratch_pool);
    }

  /* If the user specified a peg revision other than HEAD, we have to fetch
     the baseline collection url for that revision. If not, we can use the
     public url. */
  if (SVN_IS_VALID_REVNUM(revision) || fetched_rev)
    {
      SVN_ERR(svn_ra_serf__get_stable_url(&path, fetched_rev,
                                          session, NULL /* conn */,
                                          path, revision,
                                          scratch_pool, scratch_pool));
      revision = SVN_INVALID_REVNUM;
    }
  /* REVISION is always SVN_INVALID_REVNUM  */
  SVN_ERR_ASSERT(!SVN_IS_VALID_REVNUM(revision));

  gdb.path = path;

  /* If we're asked for children, fetch them now. */
  if (dirents)
    {
      /* Always request node kind to check that path is really a
       * directory. */
      if (!ret_props)
        dirent_fields |= SVN_DIRENT_KIND;

      gdb.dirents = apr_hash_make(result_pool);

      SVN_ERR(svn_ra_serf__deliver_props2(&dirent_handler,
                                          session, session->conns[0],
                                          path, SVN_INVALID_REVNUM, "1",
                                          get_dirent_props(dirent_fields,
                                                           session,
                                                           scratch_pool),
                                          get_dir_dirents_cb, &gdb,
                                          scratch_pool));

      svn_ra_serf__request_create(dirent_handler);
    }
  else
    gdb.dirents = NULL;

  if (ret_props)
    {
      gdb.ret_props = apr_hash_make(result_pool);
      SVN_ERR(svn_ra_serf__deliver_props2(&props_handler,
                                          session, session->conns[0],
                                          path, SVN_INVALID_REVNUM, "0",
                                          all_props,
                                          get_dir_props_cb, &gdb,
                                          scratch_pool));

      svn_ra_serf__request_create(props_handler);
    }
  else
    gdb.ret_props = NULL;

  if (dirent_handler)
    {
      SVN_ERR(svn_ra_serf__context_run_wait(&dirent_handler->done,
                                            session,
                                            scratch_pool));

      if (dirent_handler->sline.code != 207)
        return svn_error_trace(svn_ra_serf__unexpected_status(dirent_handler));

      if (gdb.supports_deadprop_count == svn_tristate_false
          && session->supports_deadprop_count == svn_tristate_unknown
          && dirent_fields & SVN_DIRENT_HAS_PROPS)
        {
          /* We have to requery as the server didn't give us the right
             information */
          session->supports_deadprop_count = svn_tristate_false;

          apr_hash_clear(gdb.dirents);

          SVN_ERR(svn_ra_serf__deliver_props2(&dirent_handler,
                                              session, session->conns[0],
                                              path, SVN_INVALID_REVNUM, "1",
                                              get_dirent_props(dirent_fields,
                                                               session,
                                                               scratch_pool),
                                              get_dir_dirents_cb, &gdb,
                                              scratch_pool));

          svn_ra_serf__request_create(dirent_handler);
        }
    }

  if (props_handler)
    {
      SVN_ERR(svn_ra_serf__context_run_wait(&props_handler->done,
                                            session,
                                            scratch_pool));

      if (props_handler->sline.code != 207)
        return svn_error_trace(svn_ra_serf__unexpected_status(props_handler));
    }

  /* And dirent again for the case when we had to send the request again */
  if (dirent_handler)
    {
      SVN_ERR(svn_ra_serf__context_run_wait(&dirent_handler->done,
                                            session,
                                            scratch_pool));

      if (dirent_handler->sline.code != 207)
        return svn_error_trace(svn_ra_serf__unexpected_status(dirent_handler));
    }

  if (gdb.supports_deadprop_count != svn_tristate_unknown)
    session->supports_deadprop_count = gdb.supports_deadprop_count;

  svn_pool_destroy(scratch_pool);

  if (!gdb.is_directory)
    return svn_error_create(SVN_ERR_FS_NOT_DIRECTORY, NULL,
                            _("Can't get entries of non-directory"));

  if (ret_props)
    *ret_props = gdb.ret_props;

  if (dirents)
    *dirents = gdb.dirents;

  return SVN_NO_ERROR;
}
