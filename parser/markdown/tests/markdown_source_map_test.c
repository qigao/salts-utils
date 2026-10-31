#include <salts/markdown.h>
#include <salts/markdown_html.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if(!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; \
} } while(0)

static int same_document(salts_md_rich_document_view a,
                         salts_md_rich_document_view b) {
    CHECK(a.document.source_bytes==b.document.source_bytes);
    CHECK(!memcmp(a.document.source,b.document.source,a.document.source_bytes));
    CHECK(a.raw_markdown_bytes==b.raw_markdown_bytes);
    CHECK(!memcmp(a.raw_markdown,b.raw_markdown,a.raw_markdown_bytes));
    CHECK(a.metadata_bytes==b.metadata_bytes);
    CHECK(!memcmp(a.metadata,b.metadata,a.metadata_bytes));
    CHECK(a.document.block_count==b.document.block_count);
    for(size_t i=0u;i<a.document.block_count;++i) {
        const salts_md_block *x=&a.document.blocks[i],*y=&b.document.blocks[i];
        CHECK(x->stable_id==y->stable_id && x->parent_index==y->parent_index);
        CHECK(x->kind==y->kind && x->heading_level==y->heading_level);
        CHECK(x->byte_begin==y->byte_begin && x->byte_end==y->byte_end);
        CHECK(a.block_metadata[i].byte_begin==b.block_metadata[i].byte_begin);
        CHECK(a.block_metadata[i].byte_end==b.block_metadata[i].byte_end);
    }
    for(unsigned profile=0u;profile<2u;++profile) {
        const salts_md_html_policy policy=profile?
            SALTS_MD_HTML_SPEC_CONFORMANCE:SALTS_MD_HTML_SAFE_APPLICATION;
        char first[8192],second[8192];size_t n=0u,m=0u;
        CHECK(salts_md_render_html_with_policy(&a,4096u,policy,first,
              sizeof(first),&n)==SALTS_MD_OK);
        CHECK(salts_md_render_html_with_policy(&b,4096u,policy,second,
              sizeof(second),&m)==SALTS_MD_OK);
        CHECK(n==m && !memcmp(first,second,n));
    }
    return 0;
}

static int checked_map(salts_md_document *doc) {
    const salts_md_source_map_view m=salts_md_get_source_map(doc);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(m.format_version==1u && m.node_count==v.document.block_count);
    size_t end=0u;bool complete=true;
    for(size_t i=0u;i<m.entry_count;++i) {
        const salts_md_source_map_entry *e=&m.entries[i];
        CHECK(e->semantic_begin==end && e->semantic_end>end);
        CHECK(e->semantic_end<=v.document.source_bytes);
        CHECK(e->raw_begin<=e->raw_end && e->raw_end<=v.raw_markdown_bytes);
        if(e->origin==SALTS_MD_SOURCE_UNKNOWN) {
            complete=false;CHECK(e->raw_begin==0u && e->raw_end==0u);
        } else if(e->origin==SALTS_MD_SOURCE_LITERAL) {
            CHECK(e->semantic_end-e->semantic_begin==e->raw_end-e->raw_begin);
            CHECK(!memcmp(v.document.source+e->semantic_begin,
                          v.raw_markdown+e->raw_begin,e->raw_end-e->raw_begin));
        } else CHECK(e->origin==SALTS_MD_SOURCE_TRANSFORMED);
        end=e->semantic_end;
    }
    CHECK(end==v.document.source_bytes && complete==m.semantic_complete);
    CHECK(m.nodes[0].raw_known && m.nodes[0].content_known);
    CHECK(m.nodes[0].raw_begin==0u && m.nodes[0].raw_end==v.raw_markdown_bytes);
    for(size_t i=0u;i<m.node_count;++i) {
        const salts_md_node_source *n=&m.nodes[i];
        CHECK(n->raw_begin<=n->raw_end && n->raw_end<=v.raw_markdown_bytes);
        CHECK(n->content_begin<=n->content_end && n->content_end<=v.raw_markdown_bytes);
        if(!n->raw_known)CHECK(n->raw_begin==0u && n->raw_end==0u);
        if(!n->content_known)CHECK(n->content_begin==0u && n->content_end==0u);
        if(n->raw_known && n->content_known)
            CHECK(n->content_begin>=n->raw_begin && n->content_end<=n->raw_end);
    }
    return 0;
}

static int test_exact_lexemes(void) {
    const char raw[]="# same &amp; same \\* &#x1F600; &nGt; 中😀  \r\n";
    const char *const lexemes[]={"same ","&amp;"," same ","\\*"," ",
                               "&#x1F600;"," ","&nGt;"," 中😀"};
    const char *const semantic[]={"same ","&"," same ","*"," ",
                                "😀"," ","\xe2\x89\xab\xe2\x83\x92"," 中😀"};
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse_with_source_map(raw,sizeof(raw)-1u,128u,1024u,64u,
                                        &doc)==SALTS_MD_OK);
    CHECK(!checked_map(doc));
    const salts_md_source_map_view m=salts_md_get_source_map(doc);
    const salts_md_rich_document_view v=salts_md_get_document_view(doc);
    CHECK(m.semantic_complete && m.entry_count==9u);
    size_t r=2u,s=0u;
    for(size_t i=0u;i<9u;++i) {
        const size_t nr=strlen(lexemes[i]),ns=strlen(semantic[i]);
        const salts_md_source_map_entry *e=&m.entries[i];
        CHECK(e->raw_begin==r && e->raw_end==r+nr);
        CHECK(e->semantic_begin==s && e->semantic_end==s+ns);
        CHECK(!memcmp(v.raw_markdown+r,lexemes[i],nr));
        CHECK(!memcmp(v.document.source+s,semantic[i],ns));
        CHECK(e->origin==((i==1u || i==3u || i==5u || i==7u)?
            SALTS_MD_SOURCE_TRANSFORMED:SALTS_MD_SOURCE_LITERAL));
        r+=nr;s+=ns;
    }
    CHECK(m.nodes[1].raw_known && m.nodes[1].content_known);
    CHECK(m.nodes[1].raw_begin==0u && m.nodes[1].raw_end==sizeof(raw)-1u);
    CHECK(m.nodes[1].content_begin==2u && m.nodes[1].content_end==r);
    /* Borrowed map owns neither the caller's input nor a search-derived copy. */
    salts_md_document_destroy(doc);
    return 0;
}

static int test_grammar_and_parity(void) {
    const char *const inputs[]={
        "", "same same", "same\r\nsame\r\n", "中😀  ",
        "same\r\n===\r\n", "**same** *same* ~~same~~ ` a `",
        "[same](https://example.org \"title\") ![same](image.png)",
        "[same][id]\n\n[id]: https://example.org\n",
        "> # same\r\n> - same &amp; same\r\n",
        "- # same\n  same\n  > same\n",
        "| same | same |\n| --- | --- |\n| a\\|b | &amp; |\n",
        "```c &amp;\r\nsame\r\nsame\r\n```\r\n",
        "    same\r\n    same\r\n", "<div>\r\nsame\r\n</div>\r\n",
        "*same\r\nsame*\r\n", "`same\r\nsame`", "a [same](url\n \"title\")",
        "[same][id]\n\n> [id]: https://example.org\n"
    };
    for(size_t i=0u;i<sizeof(inputs)/sizeof(inputs[0]);++i) {
        salts_md_document *plain=NULL,*mapped=NULL;
        const size_t n=strlen(inputs[i]);
        CHECK(salts_md_parse(inputs[i],n,4096u,8192u,&plain)==SALTS_MD_OK);
        CHECK(salts_md_parse_with_source_map(inputs[i],n,4096u,8192u,4096u,
                                            &mapped)==SALTS_MD_OK);
        CHECK(!checked_map(mapped));
        CHECK(!same_document(salts_md_get_document_view(plain),
                             salts_md_get_document_view(mapped)));
        CHECK(salts_md_get_source_map(plain).format_version==0u);
        salts_md_document_destroy(plain);salts_md_document_destroy(mapped);
    }
    return 0;
}

static int test_unknown_and_budgets(void) {
    const char raw[]="| a\\|b |\n| --- |\n";
    salts_md_document *doc=NULL;
    CHECK(salts_md_parse_with_source_map(raw,sizeof(raw)-1u,128u,1024u,64u,
                                        &doc)==SALTS_MD_OK);
    CHECK(!checked_map(doc));
    const salts_md_source_map_view m=salts_md_get_source_map(doc);
    CHECK(!m.semantic_complete);
    bool unknown_node=false;
    for(size_t i=1u;i<m.node_count;++i)unknown_node|=!m.nodes[i].raw_known;
    CHECK(unknown_node);
    salts_md_document_destroy(doc);
    for(size_t budget=1u;budget<5u;++budget) {
        const char text[]="a &amp; b &amp; c";
        doc=(salts_md_document*)(uintptr_t)1u;
        CHECK(salts_md_parse_with_source_map(text,sizeof(text)-1u,128u,1024u,
              budget,&doc)==SALTS_MD_CAPACITY_EXCEEDED);
        CHECK(doc==NULL);
    }
    doc=(salts_md_document*)(uintptr_t)1u;
    CHECK(salts_md_parse_with_source_map("x",1u,2u,1u,0u,&doc)==SALTS_MD_INVALID_ARGUMENT);
    CHECK(doc==NULL);
    CHECK(salts_md_parse_with_source_map("x",1u,1u,1u,1u,&doc)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(doc==NULL);
    CHECK(salts_md_parse_with_source_map("x",1u,4u,1u,SIZE_MAX,&doc)==SALTS_MD_CAPACITY_EXCEEDED);
    CHECK(doc==NULL);
    CHECK(salts_md_parse_with_source_map(NULL,1u,4u,1u,1u,&doc)==SALTS_MD_INVALID_ARGUMENT);
    CHECK(doc==NULL);
    CHECK(salts_md_parse_with_source_map(NULL,0u,1u,1u,1u,&doc)==SALTS_MD_OK);
    CHECK(salts_md_get_source_map(doc).entry_count==0u);
    CHECK(salts_md_get_source_map(doc).semantic_complete);
    salts_md_document_destroy(doc);
    CHECK(salts_md_get_source_map(NULL).format_version==0u);
    return 0;
}

static int test_node_intervals_and_ownership(void) {
    const struct {
        const char *input,*raw,*content;
        salts_md_block_kind kind;
    } cases[]={
        {"same\r\n===\r\n","same\r\n===\r\n","same",SALTS_MD_BLOCK_HEADING},
        {"> # same\r\n","# same","same",SALTS_MD_BLOCK_HEADING},
        {"- # same\n","# same","same",SALTS_MD_BLOCK_HEADING},
        {"**same**","**same**","same",SALTS_MD_BLOCK_STRONG},
        {"[same](url)","[same](url)","same",SALTS_MD_BLOCK_LINK},
        {"![same](url)","![same](url)","same",SALTS_MD_BLOCK_IMAGE},
        {"` same `","` same `"," same ",SALTS_MD_BLOCK_CODE_SPAN},
        {"```c\r\nsame\r\n```\r\n","```c\r\nsame\r\n```\r\n",
         "same\r\n",SALTS_MD_BLOCK_CODE_FENCE}
    };
    for(size_t k=0u;k<sizeof(cases)/sizeof(cases[0]);++k) {
        char input[128];const size_t length=strlen(cases[k].input);
        memcpy(input,cases[k].input,length);
        salts_md_document *doc=NULL;
        CHECK(salts_md_parse_with_source_map(input,length,128u,sizeof(input),
                                            64u,&doc)==SALTS_MD_OK);
        memset(input,'x',length); /* no borrowed caller buffer may survive */
        CHECK(!checked_map(doc));
        const salts_md_rich_document_view v=salts_md_get_document_view(doc);
        const salts_md_source_map_view m=salts_md_get_source_map(doc);
        CHECK(!memcmp(v.raw_markdown,cases[k].input,length));
        size_t found=0u;
        for(size_t i=1u;i<m.node_count;++i) {
            if(v.document.blocks[i].kind!=cases[k].kind)continue;
            const salts_md_node_source *n=&m.nodes[i];
            CHECK(n->raw_known && n->content_known);
            CHECK(n->raw_end-n->raw_begin==strlen(cases[k].raw));
            CHECK(n->content_end-n->content_begin==strlen(cases[k].content));
            CHECK(!memcmp(v.raw_markdown+n->raw_begin,cases[k].raw,strlen(cases[k].raw)));
            CHECK(!memcmp(v.raw_markdown+n->content_begin,cases[k].content,strlen(cases[k].content)));
            ++found;
        }
        CHECK(found==1u);salts_md_document_destroy(doc);
    }
    return 0;
}

int main(void) {
    if(test_exact_lexemes() || test_grammar_and_parity() ||
       test_unknown_and_budgets() || test_node_intervals_and_ownership())return 1;
    puts("Markdown source map: exact lexemes, grammar parity, UNKNOWN and atomic budgets passed");
    return 0;
}
