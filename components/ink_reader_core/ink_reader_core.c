#include "ink_reader_core.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "esp_heap_caps.h"

#define XTC_HEADER_SIZE 56u
#define XTC_INDEX_ENTRY_SIZE 16u
#define XTG_HEADER_SIZE 22u
#define XTC_MAGIC 0x00435458u
#define XTCH_MAGIC 0x48435458u
#define XTG_MAGIC 0x00475458u
#define XTH_MAGIC 0x00485458u

static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static uint64_t le64(const uint8_t *p) { return (uint64_t)le32(p)|((uint64_t)le32(p+4)<<32); }

static bool parse_header(const uint8_t *raw, size_t length, uint16_t *count, uint64_t *index_offset, uint64_t *data_offset)
{
    if (!raw || length < XTC_HEADER_SIZE || !count || !index_offset || !data_offset) return false;
    const uint32_t magic=le32(raw); const uint16_t version=le16(raw+4); *count=le16(raw+6);
    *index_offset=le64(raw+24); *data_offset=le64(raw+32);
    if ((magic!=XTC_MAGIC&&magic!=XTCH_MAGIC)||(version!=1&&version!=256)||*count==0) return false;
    const uint64_t header_size=version==256?48u:56u;
    return *index_offset>=header_size && *data_offset>*index_offset
        && *index_offset+(uint64_t)*count*XTC_INDEX_ENTRY_SIZE<=*data_offset;
}

static bool extension_ok(const char *name)
{
    const char *dot=name?strrchr(name,'.'):NULL;
    return dot && (!strcasecmp(dot,".xtc") || !strcasecmp(dot,".xtch"));
}

static bool find_in(const char *dir_path, char *path, size_t path_size)
{
    DIR *dir=opendir(dir_path); if(!dir)return false;
    char best[INK_READER_PATH_MAX]={0}; struct dirent *entry;
    while((entry=readdir(dir))!=NULL) {
        if(entry->d_name[0]=='.'||!extension_ok(entry->d_name))continue;
        if(best[0]==0||strcasecmp(entry->d_name,best)<0) snprintf(best,sizeof(best),"%s",entry->d_name);
    }
    closedir(dir); if(best[0]==0)return false;
    return snprintf(path,path_size,"%s/%s",dir_path,best)<(int)path_size;
}

void ink_reader_book_init(ink_reader_book_t *book) { if(book)memset(book,0,sizeof(*book)); }
void ink_reader_book_close(ink_reader_book_t *book)
{
    if (!book) return;
    if (book->file) fclose(book->file);
    free(book->pages);
    memset(book, 0, sizeof(*book));
}
bool ink_reader_find_first_book(char *path,size_t size)
{
    if (!path || !size) return false;
    path[0] = 0;
    return find_in("/sdcard/books",path,size)||find_in("/sdcard",path,size);
}

bool ink_reader_book_open(ink_reader_book_t *book,const char *path)
{
    if (!book || !path) return false;
    ink_reader_book_close(book);
    FILE *file=fopen(path,"rb"); if(!file)return false;
    uint8_t header[XTC_HEADER_SIZE]; uint16_t count; uint64_t index_offset,data_offset;
    if(fread(header,1,sizeof(header),file)!=sizeof(header)||!parse_header(header,sizeof(header),&count,&index_offset,&data_offset)||index_offset>LONG_MAX)goto fail;
    ink_reader_page_t *pages=heap_caps_calloc(count,sizeof(*pages),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if (!pages) pages = calloc(count, sizeof(*pages));
    if (!pages) goto fail;
    if(fseek(file,(long)index_offset,SEEK_SET)!=0){free(pages);goto fail;}
    for(uint16_t i=0;i<count;++i){
        uint8_t raw[XTC_INDEX_ENTRY_SIZE]; if(fread(raw,1,sizeof(raw),file)!=sizeof(raw)){free(pages);goto fail;}
        pages[i].offset=le64(raw); pages[i].encoded_size=le32(raw+8); pages[i].width=le16(raw+12); pages[i].height=le16(raw+14);
        if(pages[i].offset<data_offset||pages[i].encoded_size<XTG_HEADER_SIZE||pages[i].width!=480||pages[i].height!=800){free(pages);goto fail;}
    }
    book->file=file; book->pages=pages; book->page_count=count; snprintf(book->path,sizeof(book->path),"%s",path); return true;
fail: fclose(file); return false;
}

static void xth_to_bw(const uint8_t *p0,const uint8_t *p1,uint8_t *out)
{
    memset(out,0xff,INK_READER_PAGE_SIZE); const uint32_t col_bytes=100,row_bytes=60;
    for(uint16_t x=0;x<480;++x)for(uint16_t y=0;y<800;++y){
        uint32_t src=(uint32_t)(479-x)*col_bytes+y/8; uint8_t mask=(uint8_t)(0x80u>>(y&7));
        uint8_t level=(uint8_t)(((p1[src]&mask?1:0)<<1)|(p0[src]&mask?1:0));
        if(level>=2)out[(uint32_t)y*row_bytes+x/8]&=(uint8_t)~(0x80u>>(x&7));
    }
}

bool ink_reader_book_load_current(const ink_reader_book_t *book,uint8_t *buffer,size_t length)
{
    if(!book||!book->file||!buffer||length<INK_READER_PAGE_SIZE||book->current_page>=book->page_count)return false;
    const ink_reader_page_t *page=&book->pages[book->current_page]; if(page->offset>LONG_MAX||fseek(book->file,(long)page->offset,SEEK_SET)!=0)return false;
    uint8_t h[XTG_HEADER_SIZE]; if(fread(h,1,sizeof(h),book->file)!=sizeof(h))return false;
    uint32_t magic=le32(h),data_size=le32(h+10); if(le16(h+4)!=480||le16(h+6)!=800||page->encoded_size<XTG_HEADER_SIZE+data_size)return false;
    if(magic==XTG_MAGIC&&data_size==INK_READER_PAGE_SIZE)return fread(buffer,1,data_size,book->file)==data_size;
    if(magic==XTH_MAGIC&&data_size==INK_READER_PAGE_SIZE*2u){
        uint8_t *p0=heap_caps_malloc(INK_READER_PAGE_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        uint8_t *p1=heap_caps_malloc(INK_READER_PAGE_SIZE,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!p0||!p1){free(p0);free(p1);return false;}
        bool ok=fread(p0,1,INK_READER_PAGE_SIZE,book->file)==INK_READER_PAGE_SIZE&&fread(p1,1,INK_READER_PAGE_SIZE,book->file)==INK_READER_PAGE_SIZE;
        if (ok) xth_to_bw(p0, p1, buffer);
        free(p0);
        free(p1);
        return ok;
    }
    return false;
}

bool ink_reader_book_next(ink_reader_book_t *book){if(!book||book->current_page+1>=book->page_count)return false;++book->current_page;return true;}
bool ink_reader_book_previous(ink_reader_book_t *book){if(!book||book->current_page==0)return false;--book->current_page;return true;}

bool ink_reader_core_self_test(void)
{
    uint8_t h[XTC_HEADER_SIZE]={ 'X','T','C',0,1,0,2,0 };
    h[24]=56; h[32]=88; uint16_t count=0;uint64_t index=0,data=0;
    if(!parse_header(h,sizeof(h),&count,&index,&data)||count!=2||index!=56||data!=88)return false;
    h[0]='B'; if(parse_header(h,sizeof(h),&count,&index,&data))return false;
    ink_reader_book_t book={.page_count=2,.current_page=0};
    return !ink_reader_book_previous(&book)&&ink_reader_book_next(&book)&&book.current_page==1&&!ink_reader_book_next(&book)&&ink_reader_book_previous(&book);
}
