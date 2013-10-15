/*
 * svn_subr_private.h : private definitions from libsvn_subr
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

#ifndef SVN_SUBR_PRIVATE_H
#define SVN_SUBR_PRIVATE_H

#include "svn_types.h"
#include "svn_io.h"
#include "svn_config.h"


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/** Spill-to-file Buffers
 *
 * @defgroup svn_spillbuf_t Spill-to-file Buffers
 * @{
 */

/** A buffer that collects blocks of content, possibly using a file.
 *
 * The spill-buffer is created with two basic parameters: the size of the
 * blocks that will be written into the spill-buffer ("blocksize"), and
 * the (approximate) maximum size that will be allowed in memory ("maxsize").
 * Once the maxsize is reached, newly written content will be "spilled"
 * into a temporary file.
 *
 * When writing, content will be buffered into memory unless a given write
 * will cause the amount of in-memory content to exceed the specified
 * maxsize. At that point, the file is created, and the content will be
 * written to that file.
 *
 * To read information back out of a spill buffer, there are two approaches
 * available to the application:
 *
 *   *) reading blocks using svn_spillbuf_read() (a "pull" model)
 *   *) having blocks passed to a callback via svn_spillbuf_process()
 *      (a "push" model to your application)
 *
 * In both cases, the spill-buffer will provide you with a block of N bytes
 * that you must fully consume before asking for more data. The callback
 * style provides for a "stop" parameter to temporarily pause the reading
 * until another read is desired. The two styles of reading may be mixed,
 * as the caller desires. Generally, N will be the blocksize, and will be
 * less when the end of the content is reached.
 *
 * For a more stream-oriented style of reading, where the caller specifies
 * the number of bytes to read into a caller-provided buffer, please see
 * svn_spillbuf_reader_t. That overlaid type will cause more memory copies
 * to be performed (whereas the bare spill-buffer type hands you a buffer
 * to consume).
 *
 * Writes may be interleaved with reading, and content will be returned
 * in a FIFO manner. Thus, if content has been placed into the spill-buffer
 * you will always read the earliest-written data, and any newly-written
 * content will be appended to the buffer.
 *
 * Note: the file is created in the same pool where the spill-buffer was
 * created. If the content is completely read from that file, it will be
 * closed and deleted. Should writing further content cause another spill
 * file to be created, that will increase the size of the pool. There is
 * no bound on the amount of file-related resources that may be consumed
 * from the pool. It is entirely related to the read/write pattern and
 * whether spill files are repeatedly created.
 */
typedef struct svn_spillbuf_t svn_spillbuf_t;


/* Create a spill buffer.  */
svn_spillbuf_t *
svn_spillbuf__create(apr_size_t blocksize,
                     apr_size_t maxsize,
                     apr_pool_t *result_pool);

/* Create a spill buffer, with extra parameters.  */
svn_spillbuf_t *
svn_spillbuf__create_extended(apr_size_t blocksize,
                              apr_size_t maxsize,
                              svn_boolean_t delete_on_close,
                              svn_boolean_t spill_all_contents,
                              const char* dirpath,
                              apr_pool_t *result_pool);

/* Determine how much content is stored in the spill buffer.  */
svn_filesize_t
svn_spillbuf__get_size(const svn_spillbuf_t *buf);

/* Determine how much content the spill buffer is caching in memory.  */
svn_filesize_t
svn_spillbuf__get_memory_size(const svn_spillbuf_t *buf);

/* Retreive the name of the spill file. The returned value can be NULL
   if the file has not been created yet. */
const char *
svn_spillbuf__get_filename(const svn_spillbuf_t *buf);

/* Retreive the handle of the spill file. The returned value can be
   NULL if the file has not been created yet. */
apr_file_t *
svn_spillbuf__get_file(const svn_spillbuf_t *buf);

/* Write some data into the spill buffer.  */
svn_error_t *
svn_spillbuf__write(svn_spillbuf_t *buf,
                    const char *data,
                    apr_size_t len,
                    apr_pool_t *scratch_pool);


/* Read a block of memory from the spill buffer. @a *data will be set to
   NULL if no content remains. Otherwise, @a data and @a len will point to
   data that must be fully-consumed by the caller. This data will remain
   valid until another call to svn_spillbuf_write(), svn_spillbuf_read(),
   or svn_spillbuf_process(), or if the spill buffer's pool is cleared.  */
svn_error_t *
svn_spillbuf__read(const char **data,
                   apr_size_t *len,
                   svn_spillbuf_t *buf,
                   apr_pool_t *scratch_pool);


/* Callback for reading content out of the spill buffer. Set @a stop if
   you want to stop the processing (and will call svn_spillbuf_process
   again, at a later time).  */
typedef svn_error_t * (*svn_spillbuf_read_t)(svn_boolean_t *stop,
                                             void *baton,
                                             const char *data,
                                             apr_size_t len,
                                             apr_pool_t *scratch_pool);


/* Process the content stored in the spill buffer. @a exhausted will be
   set to TRUE if all of the content is processed by @a read_func. This
   function may return early if the callback returns TRUE for its 'stop'
   parameter.  */
svn_error_t *
svn_spillbuf__process(svn_boolean_t *exhausted,
                      svn_spillbuf_t *buf,
                      svn_spillbuf_read_t read_func,
                      void *read_baton,
                      apr_pool_t *scratch_pool);


/** Classic stream reading layer on top of spill-buffers.
 *
 * This type layers upon a spill-buffer to enable a caller to read a
 * specified number of bytes into the caller's provided buffer. This
 * implies more memory copies than the standard spill-buffer reading
 * interface, but is sometimes required by spill-buffer users.
 */
typedef struct svn_spillbuf_reader_t svn_spillbuf_reader_t;


/* Create a spill-buffer and a reader for it.  */
svn_spillbuf_reader_t *
svn_spillbuf__reader_create(apr_size_t blocksize,
                            apr_size_t maxsize,
                            apr_pool_t *result_pool);

/* Read @a len bytes from @a reader into @a data. The number of bytes
   actually read is stored in @a amt. If the content is exhausted, then
   @a amt is set to zero. It will always be non-zero if the spill-buffer
   contains content.

   If @a len is zero, then SVN_ERR_INCORRECT_PARAMS is returned.  */
svn_error_t *
svn_spillbuf__reader_read(apr_size_t *amt,
                          svn_spillbuf_reader_t *reader,
                          char *data,
                          apr_size_t len,
                          apr_pool_t *scratch_pool);


/* Read a single character from @a reader, and place it in @a c. If there
   is no content in the spill-buffer, then SVN_ERR_STREAM_UNEXPECTED_EOF
   is returned.  */
svn_error_t *
svn_spillbuf__reader_getc(char *c,
                          svn_spillbuf_reader_t *reader,
                          apr_pool_t *scratch_pool);


/* Write @a len bytes from @a data into the spill-buffer in @a reader.  */
svn_error_t *
svn_spillbuf__reader_write(svn_spillbuf_reader_t *reader,
                           const char *data,
                           apr_size_t len,
                           apr_pool_t *scratch_pool);


/* Return a stream built on top of a spillbuf, using the same arguments as
   svn_spillbuf__create().  This stream can be used for reading and writing,
   but implements the same basic sematics of a spillbuf for the underlying
   storage. */
svn_stream_t *
svn_stream__from_spillbuf(svn_spillbuf_t *buf,
                          apr_pool_t *result_pool);

/** @} */

/**
 * Internal function for creating a MD5 checksum from a binary digest.
 *
 * @since New in 1.8
 */
svn_checksum_t *
svn_checksum__from_digest_md5(const unsigned char *digest,
                              apr_pool_t *result_pool);

/**
 * Internal function for creating a SHA1 checksum from a binary
 * digest.
 *
 * @since New in 1.8
 */
svn_checksum_t *
svn_checksum__from_digest_sha1(const unsigned char *digest,
                               apr_pool_t *result_pool);


/**
 * @defgroup svn_hash_support Hash table serialization support
 * @{
 */

/*----------------------------------------------------*/

/**
 * @defgroup svn_hash_misc Miscellaneous hash APIs
 * @{
 */

/** @} */


/**
 * @defgroup svn_hash_getters Specialized getter APIs for hashes
 * @{
 */

/** Find the value of a @a key in @a hash, return the value.
 *
 * If @a hash is @c NULL or if the @a key cannot be found, the
 * @a default_value will be returned.
 *
 * @since New in 1.7.
 */
const char *
svn_hash__get_cstring(apr_hash_t *hash,
                      const char *key,
                      const char *default_value);

/** Like svn_hash_get_cstring(), but for boolean values.
 *
 * Parses the value as a boolean value. The recognized representations
 * are 'TRUE'/'FALSE', 'yes'/'no', 'on'/'off', '1'/'0'; case does not
 * matter.
 *
 * @since New in 1.7.
 */
svn_boolean_t
svn_hash__get_bool(apr_hash_t *hash,
                   const char *key,
                   svn_boolean_t default_value);

/** @} */

/**
 * @defgroup svn_hash_create Create optimized APR hash tables
 * @{
 */

/** Returns a hash table, allocated in @a pool, with the same ordering of
 * elements as APR 1.4.5 or earlier (using apr_hashfunc_default) but uses
 * a faster hash function implementation.
 *
 * @since New in 1.8.
 */
apr_hash_t *
svn_hash__make(apr_pool_t *pool);

/** @} */

/** @} */


/** Apply the changes described by @a prop_changes to @a original_props and
 * return the result.  The inverse of svn_prop_diffs().
 *
 * Allocate the resulting hash from @a pool, but allocate its keys and
 * values from @a pool and/or by reference to the storage of the inputs.
 *
 * Note: some other APIs use an array of pointers to svn_prop_t.
 *
 * @since New in 1.8.
 */
apr_hash_t *
svn_prop__patch(const apr_hash_t *original_props,
                const apr_array_header_t *prop_changes,
                apr_pool_t *pool);


/**
 * @defgroup svn_version Version number dotted triplet parsing
 * @{
 */

/* Set @a *version to a version structure parsed from the version
 * string representation in @a version_string.  Return
 * @c SVN_ERR_MALFORMED_VERSION_STRING if the string fails to parse
 * cleanly.
 *
 * @since New in 1.8.
 */
svn_error_t *
svn_version__parse_version_string(svn_version_t **version,
                                  const char *version_string,
                                  apr_pool_t *result_pool);

/* Return true iff @a version represents a version number of at least
 * the level represented by @a major, @a minor, and @a patch.
 *
 * @since New in 1.8.
 */
svn_boolean_t
svn_version__at_least(svn_version_t *version,
                      int major,
                      int minor,
                      int patch);

/** @} */

/**
 * @defgroup svn_compress Data (de-)compression API
 * @{
 */

/* This is at least as big as the largest size of an integer that
   svn__encode_uint() can generate; it is sufficient for creating buffers
   for it to write into.  This assumes that integers are at most 64 bits,
   and so 10 bytes (with 7 bits of information each) are sufficient to
   represent them. */
#define SVN__MAX_ENCODED_UINT_LEN 10

/* Compression method parameters for svn__encode_uint. */

/* No compression (but a length prefix will still be added to the buffer) */
#define SVN__COMPRESSION_NONE         0

/* Fastest, least effective compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_MIN     1

/* Default compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_DEFAULT 5

/* Slowest, best compression method & level provided by zlib. */
#define SVN__COMPRESSION_ZLIB_MAX     9

/* Encode VAL into the buffer P using the variable-length 7b/8b unsigned
   integer format.  Return the incremented value of P after the
   encoded bytes have been written.  P must point to a buffer of size
   at least SVN__MAX_ENCODED_UINT_LEN.

   This encoding uses the high bit of each byte as a continuation bit
   and the other seven bits as data bits.  High-order data bits are
   encoded first, followed by lower-order bits, so the value can be
   reconstructed by concatenating the data bits from left to right and
   interpreting the result as a binary number.  Examples (brackets
   denote byte boundaries, spaces are for clarity only):

           1 encodes as [0 0000001]
          33 encodes as [0 0100001]
         129 encodes as [1 0000001] [0 0000001]
        2000 encodes as [1 0001111] [0 1010000]
*/
unsigned char *
svn__encode_uint(unsigned char *p, apr_uint64_t val);

/* Decode an unsigned 7b/8b-encoded integer into *VAL and return a pointer
   to the byte after the integer.  The bytes to be decoded live in the
   range [P..END-1].  If these bytes do not contain a whole encoded
   integer, return NULL; in this case *VAL is undefined.

   See the comment for svn__encode_uint() earlier in this file for more
   detail on the encoding format.  */
const unsigned char *
svn__decode_uint(apr_uint64_t *val,
                 const unsigned char *p,
                 const unsigned char *end);

/* Get the data from IN, compress it according to the specified
 * COMPRESSION_METHOD and write the result to OUT.
 * SVN__COMPRESSION_NONE is valid for COMPRESSION_METHOD.
 */
svn_error_t *
svn__compress(svn_stringbuf_t *in,
              svn_stringbuf_t *out,
              int compression_method);

/* Get the compressed data from IN, decompress it and write the result to
 * OUT.  Return an error if the decompressed size is larger than LIMIT.
 */
svn_error_t *
svn__decompress(svn_stringbuf_t *in,
                svn_stringbuf_t *out,
                apr_size_t limit);

/** @} */

/**
 * @defgroup svn_root_pools Recycle-able root pools API
 * @{
 */

/* Opaque thread-safe container for unused / recylcleable root pools.
 *
 * Recyling root pools (actually, their allocators) circumvents a
 * scalability bottleneck in the OS memory management when multi-threaded
 * applications frequently create and destroy allocators.
 */
typedef struct svn_root_pools__t svn_root_pools__t;

/* Create a new root pools container and return it in *POOLS.
 */
svn_error_t *
svn_root_pools__create(svn_root_pools__t **pools);

/* Return a currently unused pool from POOLS.  If POOLS is empty, create a
 * new root pool and return that.  The pool returned is not thread-safe.
 */
apr_pool_t *
svn_root_pools__acquire_pool(svn_root_pools__t *pools);

/* Clear and release the given root POOL and put it back into POOLS.
 * If that fails, destroy POOL.
 */
void
svn_root_pools__release_pool(apr_pool_t *pool,
                             svn_root_pools__t *pools);

/** @} */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVN_SUBR_PRIVATE_H */
