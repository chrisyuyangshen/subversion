/* utils.h --- wc/client test utilities
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

#ifndef SVN_TEST_UTILS_H
#define SVN_TEST_UTILS_H

#include <apr_pools.h>
#include "svn_error.h"
#include "../svn_test.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/*-------------------------------------------------------------------*/

/** Helper routines for creating repositories and WCs. **/


#define REPOSITORIES_WORK_DIR "svn-test-work/repositories"
#define WCS_WORK_DIR "svn-test-work/working-copies"


/* The "sandbox" is a work space including a working copy and a repository.
 * Functions are provided for easy manipulation of the WC.  Paths given to
 * these functions can be relative to the WC root as stored in the sandbox
 * object, or can be absolute paths. */

/* An object holding the state of a test sand-box. */
typedef struct svn_test__sandbox_t
{
  /* The WC context object. */
  svn_wc_context_t *wc_ctx;
  /* The repository URL. */
  const char *repos_url;
  /* The absolute local path of the WC root. */
  const char *wc_abspath;
  /* A pool that can be used for all allocations. */
  apr_pool_t *pool;
} svn_test__sandbox_t;


/* Create an empty repository and WC for the test TEST_NAME.  Fill in
 * *SANDBOX with all the details.
 *
 * Create the repository and WC in subdirectories called
 * REPOSITORIES_WORK_DIR/TEST_NAME and WCS_WORK_DIR/TEST_NAME respectively,
 * within the current working directory.
 *
 * Register the repo and WC to be cleaned up when the test suite exits. */
svn_error_t *
svn_test__sandbox_create(svn_test__sandbox_t *sandbox,
                         const char *test_name,
                         const svn_test_opts_t *opts,
                         apr_pool_t *pool);

/* ---------------------------------------------------------------------- */
/* Functions for easy manipulation of a WC. Paths given to these functions
 * can be relative to the WC root as stored in the WC baton. */

/* Return the abspath of PATH which is absolute or relative to the WC in B. */
#define sbox_wc_path(b, path) \
          (svn_dirent_join((b)->wc_abspath, (path), (b)->pool))

/* Create a file on disk at PATH, with TEXT as its content. */
void
sbox_file_write(svn_test__sandbox_t *b, const char *path, const char *text);

/* Schedule for addition the single node that exists on disk at PATH,
 * non-recursively. */
svn_error_t *
sbox_wc_add(svn_test__sandbox_t *b, const char *path);

/* Create a single directory on disk. */
svn_error_t *
sbox_disk_mkdir(svn_test__sandbox_t *b, const char *path);

/* Create a single directory on disk and schedule it for addition. */
svn_error_t *
sbox_wc_mkdir(svn_test__sandbox_t *b, const char *path);

/* Copy the WC file or directory tree FROM_PATH to TO_PATH which must not
 * exist beforehand. */
svn_error_t *
sbox_wc_copy(svn_test__sandbox_t *b, const char *from_path, const char *to_path);

/* Revert a WC file or directory tree at PATH */
svn_error_t *
sbox_wc_revert(svn_test__sandbox_t *b, const char *path, svn_depth_t depth);

/* */
svn_error_t *
sbox_wc_delete(svn_test__sandbox_t *b, const char *path);

/* */
svn_error_t *
sbox_wc_exclude(svn_test__sandbox_t *b, const char *path);

/* */
svn_error_t *
sbox_wc_commit(svn_test__sandbox_t *b, const char *path);

/* */
svn_error_t *
sbox_wc_commit_ex(svn_test__sandbox_t *b,
                  apr_array_header_t *targets,
                  svn_depth_t depth);

/* */
svn_error_t *
sbox_wc_update(svn_test__sandbox_t *b, const char *path, svn_revnum_t revnum);

svn_error_t *
sbox_wc_update_depth(svn_test__sandbox_t *b,
                     const char *path,
                     svn_revnum_t revnum,
                     svn_depth_t depth,
                     svn_boolean_t sticky);

svn_error_t *
sbox_wc_switch(svn_test__sandbox_t *b,
               const char *path,
               const char *url,
               svn_depth_t depth);

/* */
svn_error_t *
sbox_wc_resolved(svn_test__sandbox_t *b, const char *path);

/* */
svn_error_t *
sbox_wc_resolve(svn_test__sandbox_t *b, const char *path, svn_depth_t depth,
                svn_wc_conflict_choice_t conflict_choice);

/* */
svn_error_t *
sbox_wc_move(svn_test__sandbox_t *b, const char *src, const char *dst);

/* Set property NAME to VALUE on PATH. If VALUE=NULL, delete the property. */
svn_error_t *
sbox_wc_propset(svn_test__sandbox_t *b,
           const char *name,
           const char *value,
           const char *path);

/* Create the Greek tree on disk in the WC, and commit it. */
svn_error_t *
sbox_add_and_commit_greek_tree(svn_test__sandbox_t *b);


/* Create a WC directory at WC_ABSPATH containing a fake WC DB, generated by
 * executing the SQL statements EXTRA_STATEMENTS in addition to the standard
 * WC DB schema. */
svn_error_t *
svn_test__create_fake_wc(const char *wc_abspath,
                         const char *extra_statements,
                         apr_pool_t *scratch_pool);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif  /* SVN_TEST_UTILS_H */
