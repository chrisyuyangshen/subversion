/*
 * svn_string.h :  counted_length strings for Subversion
 *                 (using apr's memory pools)
 *
 * ================================================================
 * Copyright (c) 2000 CollabNet.  All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 
 * 3. The end-user documentation included with the redistribution, if
 * any, must include the following acknowlegement: "This product includes
 * software developed by CollabNet (http://www.Collab.Net)."
 * Alternately, this acknowlegement may appear in the software itself, if
 * and wherever such third-party acknowlegements normally appear.
 * 
 * 4. The hosted project names must not be used to endorse or promote
 * products derived from this software without prior written
 * permission. For written permission, please contact info@collab.net.
 * 
 * 5. Products derived from this software may not use the "Tigris" name
 * nor may "Tigris" appear in their names without prior written
 * permission of CollabNet.
 * 
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESSED OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL COLLABNET OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ====================================================================
 * 
 * This software consists of voluntary contributions made by many
 * individuals on behalf of CollabNet.
 */


#ifndef SVN_STRING_H
#define SVN_STRING_H

#include <apr_pools.h>       /* APR memory pools for everyone. */
#include "svn_types.h"



/* The svn_string_t data type.  Pretty much what you expected. */
typedef struct svn_string_t
{
  char *data;                /* pointer to the bytestring */
  apr_size_t len;            /* length of bytestring */
  apr_size_t blocksize;      /* total size of buffer allocated */
} svn_string_t;



/* Create a new bytestring containing a C string (null-terminated), or
   containing a generic string of bytes (NON-null-terminated) */

svn_string_t * svn_string_create (const char *cstring, 
                                  apr_pool_t *pool);
svn_string_t * svn_string_ncreate (const char *bytes, const apr_size_t size, 
                                   apr_pool_t *pool);

/* Set/get  a bytestring empty */

void svn_string_setempty (svn_string_t *str);
svn_boolean_t svn_string_isempty (const svn_string_t *str);
void svn_string_chop (svn_string_t *str, apr_size_t bytes);

/* Fill bytestring with a character */

void svn_string_fillchar (svn_string_t *str, const unsigned char c);

/* Append either a string of bytes, an svn_string_t, or a C-string
   onto a svn_string_t.  reallocs() if necessary. */

void svn_string_appendbytes (svn_string_t *str, const char *bytes, 
                             const size_t count, apr_pool_t *pool);
void svn_string_appendstr (svn_string_t *targetstr, 
                           const svn_string_t *appendstr,
                           apr_pool_t *pool);
void svn_string_appendcstr (svn_string_t *targetstr,
                            const char *cstr,
                            apr_pool_t *pool);


/* Duplicate a bytestring;  returns freshly malloc'd copy.  */

svn_string_t * svn_string_dup (const svn_string_t *original_string,
                               apr_pool_t *pool);


/* compare if two bytestrings' data fields are identical,
   byte-for-byte */

svn_boolean_t svn_string_compare (const svn_string_t *str1, 
                                  const svn_string_t *str2);

/* convenience routines */

apr_size_t svn_string_first_non_whitespace (const svn_string_t *str);
void svn_string_strip_whitespace (svn_string_t *str);

/* Return position of last occurrence of CHAR in STR, or return
   STR->len if no occurrence. */ 
apr_size_t svn_string_find_char_backward (svn_string_t *str, char ch);

/* Chop STR back to CHAR, inclusive.  Returns number of chars
   chopped, so if no such CHAR in STR, chops nothing and returns 0. */
apr_size_t svn_string_chop_back_to_char (svn_string_t *str, char ch);
#endif  /* SVN_STRING_H */


/* --------------------------------------------------------------
 * local variables:
 * eval: (load-file "../svn-dev.el")
 * end:
 */
