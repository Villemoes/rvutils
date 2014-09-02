#ifndef JENKINS_HASH_H_INCLUDED
#define JENKINS_HASH_H_INCLUDED

/* Code from Bob Jenkins, http://burtleburtle.net/bob/hash/index.html */

#include <stdint.h>
#include <stddef.h>

/**
 * hashword - hash an array of uint32_t to a single uint32_t value
 *
 * @k        - the key, an array of uint32_t values
 * @length   - the length of the key, in uint32_ts
 * @initval  - the previous hash, or an arbitrary value
 */
uint32_t jenkins_hashword(const uint32_t *k, size_t length, uint32_t initval);

/**
 * hashword2 - compute two hash values simultaneously
 *
 * Same as hashword(), but takes two seeds and returns two 32-bit
 * values. @pc and @pb must both be nonnull, and *@pc and *@pb must
 * both be initialized with seeds.
 *
 * @k      - the key, an array of uint32_t values 
 * @length - the length of the key, in uint32_ts 
 * @pc     - IN: seed OUT: primary hash value 
 * @pb     - IN: more seed OUT: secondary hash value 
 */
void jenkins_hashword2(const uint32_t *k, size_t length, uint32_t *pc, uint32_t *pb);

/**
 * hash - hash a variable-length key into a 32-bit value
 *
 * @k       - the key (the unaligned variable-length array of bytes)
 * @length  - the length of the key, counting by bytes
 * @initval - can be any 4-byte value
 *
 * Returns a 32-bit value.  Every bit of the key affects every bit of
 * the return value.  Two keys differing by one or two bits will have
 * totally different hash values.
 *
 */
uint32_t jenkins_hash(const void *key, size_t length, uint32_t initval);

/**
 * hash2 - return two 32-bit hash values
 *
 * This is identical to hash(), except it returns two 32-bit hash
 * values instead of just one.  This is good enough for hash table
 * lookup with 2^^64 buckets, or if you want a second hash if you're not
 * happy with the first, or if you want a probably-unique 64-bit ID for
 * the key.  *pc is better mixed than *pb, so use *pc first.  If you want
 * a 64-bit value do something like "*pc + (((uint64_t)*pb)<<32)".
 */
void jenkins_hash2(const void *key, size_t length, uint32_t *pc, uint32_t *pb);

#endif /* !JENKINS_HASH_H_INCLUDED */
