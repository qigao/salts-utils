/* Compile the canonical implementation with test-local allocation failure
 * injection; there is no alternative grammar or production allocator hook.
 * Track parser allocations; the real Salts::Unicode dependency links normally.
 * Every failure must leave *out=NULL and release all parser ownership.
 */
#include <salts/markdown.h>
#include <salts_unicode.h>
#include <vstr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

enum { TEST_ALLOCATION_MAX=8192u };
static size_t calls,fail_at,live;
static void *owned[TEST_ALLOCATION_MAX];

static void remember(void *p) {
    if(!p)return;
    for(size_t i=0u;i<TEST_ALLOCATION_MAX;++i) {
        if(!owned[i]) {owned[i]=p;++live;return;}
    }
    abort();
}
static size_t slot(void *p) {
    for(size_t i=0u;i<TEST_ALLOCATION_MAX;++i)
        if(owned[i]==p)return i;
    abort();
}
static void *test_malloc(size_t n) {
    if(++calls==fail_at)return NULL;
    void *p=malloc(n);remember(p);return p;
}
static void *test_calloc(size_t n,size_t z) {
    if(++calls==fail_at)return NULL;
    void *p=calloc(n,z);remember(p);return p;
}
static void *test_realloc(void *p,size_t n) {
    if(!n)abort(); /* canonical parser never requests zero-size realloc */
    if(++calls==fail_at)return NULL;
    const size_t i=p?slot(p):TEST_ALLOCATION_MAX;
    void *q=realloc(p,n);
    if(q) {
        if(i<TEST_ALLOCATION_MAX)owned[i]=q;
        else remember(q);
    }
    return q;
}
static void test_free(void *p) {
    if(p) {owned[slot(p)]=NULL;--live;}
    free(p);
}
#define malloc test_malloc
#define calloc test_calloc
#define realloc test_realloc
#define free test_free
#include "../src/markdown_c11.c"
#undef malloc
#undef calloc
#undef realloc
#undef free

int main(void) {
    /* Force node/map reallocations beyond the initial capacities as well as
     * first allocation, entity expansion and container-definition replay. */
    char many[801];
    for(size_t i=0u;i<100u;++i)memcpy(many+i*8u,"a &amp; ",8u);
    many[800]='\0';
    const char *inputs[]={many,"# same &amp; same \\* 😀\r\n",
        "[same][id]\n\n> [id]: https://example.org\n",
        "| a\\|b |\n| --- |\n",
        "**same** [same](url \"title\") `a\nb`\n"};
    size_t failures=0u;
    for(size_t k=0u;k<sizeof(inputs)/sizeof(inputs[0]);++k) {
        salts_md_document *d=NULL;
        calls=fail_at=0u;
        if(salts_md_parse_with_source_map(inputs[k],strlen(inputs[k]),
                4096u,8192u,4096u,&d)!=SALTS_MD_OK)return 2;
        const size_t count=calls;
        salts_md_document_destroy(d);
        if(live)return 3;
        for(size_t i=1u;i<=count;++i) {
            calls=0u;fail_at=i;d=(salts_md_document*)(uintptr_t)1u;
            const salts_md_status status=salts_md_parse_with_source_map(
                inputs[k],strlen(inputs[k]),4096u,8192u,4096u,&d);
            if(status!=SALTS_MD_OUT_OF_MEMORY || d || live) {
                fprintf(stderr,"fixture=%zu allocation=%zu/%zu status=%d doc=%p live=%zu\n",
                        k,i,count,(int)status,(void*)d,live);
                return 4;
            }
            ++failures;
        }
    }
    printf("source-map allocation failures: %zu/%zu atomic, no retained parser allocations\n",
           failures,failures);
    return 0;
}
