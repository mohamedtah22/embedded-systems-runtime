#include "fw_image.h"
#include "embedded_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define FW_MAGIC 0x4d4c4657u /* MLFW */
#define FW_FORMAT_VERSION 1u
#define FW_HEADER_SIZE 40u
#define FW_MAX_IMAGE (64u * 1024u * 1024u)

typedef enum {
    FW_ARCH_GENERIC = 0,
    FW_ARCH_X86_64 = 1,
    FW_ARCH_ARM64 = 2,
    FW_ARCH_I386 = 3
} fw_arch;

typedef struct {
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t arch;
    uint64_t image_size;
    uint32_t crc32;
} fw_header;

static void put_u16_le(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void put_u32_le(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void put_u64_le(uint8_t *p, uint64_t v) { for (int i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static uint16_t get_u16_le(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1]<<8)); }
static uint32_t get_u32_le(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t get_u64_le(const uint8_t *p) { uint64_t v=0; for (int i=7;i>=0;i--) v=(v<<8)|p[i]; return v; }

static const char *arch_name(uint16_t arch) {
    switch (arch) {
        case FW_ARCH_X86_64: return "x86_64";
        case FW_ARCH_ARM64: return "arm64";
        case FW_ARCH_I386: return "i386";
        default: return "generic";
    }
}

static int arch_parse(const char *s, uint16_t *arch) {
    if (strcmp(s, "generic") == 0) *arch = FW_ARCH_GENERIC;
    else if (strcmp(s, "x86_64") == 0) *arch = FW_ARCH_X86_64;
    else if (strcmp(s, "arm64") == 0 || strcmp(s, "aarch64") == 0) *arch = FW_ARCH_ARM64;
    else if (strcmp(s, "i386") == 0 || strcmp(s, "x86") == 0) *arch = FW_ARCH_I386;
    else return -1;
    return 0;
}

static int version_parse(const char *s, fw_header *h) {
    unsigned a,b,c;
    char extra;
    if (sscanf(s, "%u.%u.%u%c", &a,&b,&c,&extra) != 3 || a>65535 || b>65535 || c>65535) return -1;
    h->major=(uint16_t)a; h->minor=(uint16_t)b; h->patch=(uint16_t)c;
    return 0;
}

static void serialize_header(const fw_header *h, uint8_t out[FW_HEADER_SIZE]) {
    memset(out, 0, FW_HEADER_SIZE);
    put_u32_le(out + 0, FW_MAGIC);
    put_u16_le(out + 4, FW_FORMAT_VERSION);
    put_u16_le(out + 6, h->arch);
    put_u16_le(out + 8, h->major);
    put_u16_le(out + 10, h->minor);
    put_u16_le(out + 12, h->patch);
    put_u16_le(out + 14, FW_HEADER_SIZE);
    put_u64_le(out + 16, h->image_size);
    put_u32_le(out + 24, h->crc32);
    put_u32_le(out + 28, 0);
    put_u64_le(out + 32, 0);
}

static int parse_header(const uint8_t in[FW_HEADER_SIZE], fw_header *h) {
    if (get_u32_le(in) != FW_MAGIC) return -1;
    if (get_u16_le(in + 4) != FW_FORMAT_VERSION || get_u16_le(in + 14) != FW_HEADER_SIZE) return -1;
    memset(h, 0, sizeof(*h));
    h->arch = get_u16_le(in + 6);
    h->major = get_u16_le(in + 8);
    h->minor = get_u16_le(in + 10);
    h->patch = get_u16_le(in + 12);
    h->image_size = get_u64_le(in + 16);
    h->crc32 = get_u32_le(in + 24);
    return 0;
}

static int copy_and_crc(int in_fd, int out_fd, uint64_t expected, uint32_t *crc_out) {
    uint8_t buf[8192];
    uint64_t total=0;
    uint32_t crc=mlrt_crc32_begin();
    for (;;) {
        ssize_t n=read(in_fd, buf, sizeof(buf));
        if (n<0) { if(errno==EINTR) continue; return -1; }
        if (n==0) break;
        total += (uint64_t)n;
        if (total > expected) return -1;
        crc=mlrt_crc32_update(crc, buf, (size_t)n);
        if (out_fd >= 0) {
            size_t off=0;
            while(off<(size_t)n) {
                ssize_t w=write(out_fd, buf+off, (size_t)n-off);
                if(w<0) { if(errno==EINTR) continue; return -1; }
                off += (size_t)w;
            }
        }
    }
    if (total != expected) return -1;
    *crc_out=mlrt_crc32_end(crc);
    return 0;
}

static int pack_image(const char *input, const char *output, const fw_header *meta) {
    int in=open(input,O_RDONLY|O_CLOEXEC);
    if(in<0){perror("fw pack input");return 1;}
    struct stat st;
    if(fstat(in,&st)!=0 || st.st_size<0 || (uint64_t)st.st_size>FW_MAX_IMAGE){fprintf(stderr,"fw pack: invalid or oversized input\n");close(in);return 1;}

    int out=open(output,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644);
    if(out<0){perror("fw pack output");close(in);return 1;}
    uint8_t zero[FW_HEADER_SIZE]={0};
    if(write(out,zero,sizeof(zero))!=(ssize_t)sizeof(zero)){perror("fw pack header");close(in);close(out);return 1;}

    fw_header h=*meta;
    h.image_size=(uint64_t)st.st_size;
    if(copy_and_crc(in,out,h.image_size,&h.crc32)!=0){fprintf(stderr,"fw pack: copy failed\n");close(in);close(out);return 1;}
    close(in);

    uint8_t hdr[FW_HEADER_SIZE];
    serialize_header(&h,hdr);
    if(lseek(out,0,SEEK_SET)<0 || write(out,hdr,sizeof(hdr))!=(ssize_t)sizeof(hdr)){perror("fw pack finalize");close(out);return 1;}
    close(out);
    printf("Packed firmware image\n  output: %s\n  version: %u.%u.%u\n  architecture: %s\n  image_size: %" PRIu64 "\n  crc32: 0x%08x\n",
           output,h.major,h.minor,h.patch,arch_name(h.arch),h.image_size,h.crc32);
    return 0;
}

static int read_image_header(int fd, fw_header *h) {
    uint8_t buf[FW_HEADER_SIZE];
    size_t off=0;
    while(off<sizeof(buf)) {
        ssize_t n=read(fd,buf+off,sizeof(buf)-off);
        if(n<0){if(errno==EINTR)continue;return -1;}
        if(n==0)return -1;
        off+=(size_t)n;
    }
    return parse_header(buf,h);
}

static int inspect_or_verify(const char *path, int verify) {
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0){perror("fw image");return 1;}
    fw_header h;
    if(read_image_header(fd,&h)!=0){fprintf(stderr,"fw: invalid MLFW image header\n");close(fd);return 1;}
    struct stat st;
    if(fstat(fd,&st)!=0){perror("fw stat");close(fd);return 1;}
    uint64_t expected_file=FW_HEADER_SIZE+h.image_size;
    int size_ok=(uint64_t)st.st_size==expected_file;
    uint32_t actual=0;
    int crc_ok=0;
    if(size_ok && h.image_size<=FW_MAX_IMAGE && copy_and_crc(fd,-1,h.image_size,&actual)==0) crc_ok=(actual==h.crc32);

    printf("Firmware Image\n  version: %u.%u.%u\n  architecture: %s\n  image_size: %" PRIu64 " bytes\n  stored_crc32: 0x%08x\n",
           h.major,h.minor,h.patch,arch_name(h.arch),h.image_size,h.crc32);
    if(verify){
        printf("  file_size: %s\n  crc32: %s\n  status: %s\n",size_ok?"OK":"INVALID",crc_ok?"OK":"INVALID",(size_ok&&crc_ok)?"VALID":"INVALID");
    }
    close(fd);
    return verify && !(size_ok&&crc_ok) ? 1 : 0;
}

static int verify_image_file(const char *path, fw_header *h, int *size_ok_out, int *crc_ok_out) {
    int fd=open(path,O_RDONLY|O_CLOEXEC);
    if(fd<0){perror("fw image");return -1;}
    if(read_image_header(fd,h)!=0){fprintf(stderr,"fw: invalid MLFW image header\n");close(fd);return -1;}
    struct stat st;
    if(fstat(fd,&st)!=0){perror("fw stat");close(fd);return -1;}
    uint64_t expected_file=FW_HEADER_SIZE+h->image_size;
    int size_ok=(uint64_t)st.st_size==expected_file;
    uint32_t actual=0;
    int crc_ok=0;
    if(size_ok && h->image_size<=FW_MAX_IMAGE && copy_and_crc(fd,-1,h->image_size,&actual)==0) crc_ok=(actual==h->crc32);
    close(fd);
    if(size_ok_out)*size_ok_out=size_ok;
    if(crc_ok_out)*crc_ok_out=crc_ok;
    return 0;
}

static int semver_cmp(const fw_header *a, const fw_header *b) {
    if(a->major!=b->major)return a->major>b->major?1:-1;
    if(a->minor!=b->minor)return a->minor>b->minor?1:-1;
    if(a->patch!=b->patch)return a->patch>b->patch?1:-1;
    return 0;
}

static int check_update_policy(const char *path, const char *current, int allow_downgrade) {
    fw_header candidate,current_h={0};
    int size_ok=0,crc_ok=0;
    if(version_parse(current,&current_h)!=0){fprintf(stderr,"fw check-update: invalid current version\n");return 2;}
    if(verify_image_file(path,&candidate,&size_ok,&crc_ok)!=0)return 1;
    if(!size_ok||!crc_ok){printf("Update policy: REJECT (invalid image integrity)\n");return 1;}
    int cmp=semver_cmp(&candidate,&current_h);
    int accepted=allow_downgrade || cmp>0;
    printf("Firmware Update Policy\n");
    printf("  current: %u.%u.%u\n",current_h.major,current_h.minor,current_h.patch);
    printf("  candidate: %u.%u.%u\n",candidate.major,candidate.minor,candidate.patch);
    printf("  integrity: VALID\n");
    printf("  anti_rollback: %s\n",allow_downgrade?"disabled":"enabled");
    printf("  decision: %s\n",accepted?"ACCEPT":"REJECT");
    return accepted?0:1;
}

static void usage(FILE *out){
    fprintf(out,
      "usage:\n"
      "  mlrt fw pack INPUT OUTPUT [--version X.Y.Z] [--arch generic|x86_64|arm64|i386]\n"
      "  mlrt fw inspect IMAGE\n"
      "  mlrt fw verify IMAGE\n"
      "  mlrt fw check-update IMAGE --current X.Y.Z [--allow-downgrade]\n");
}

int fw_cli_main(int argc,char **argv){
    if(argc<2){usage(stderr);return 2;}
    if(strcmp(argv[1],"pack")==0){
        if(argc<4){usage(stderr);return 2;}
        fw_header h={.major=1,.minor=0,.patch=0,.arch=FW_ARCH_GENERIC};
        for(int i=4;i<argc;i++){
            if(strcmp(argv[i],"--version")==0 && i+1<argc){if(version_parse(argv[++i],&h)!=0){fprintf(stderr,"invalid version\n");return 2;}}
            else if(strcmp(argv[i],"--arch")==0 && i+1<argc){if(arch_parse(argv[++i],&h.arch)!=0){fprintf(stderr,"invalid architecture\n");return 2;}}
            else {usage(stderr);return 2;}
        }
        return pack_image(argv[2],argv[3],&h);
    }
    if(strcmp(argv[1],"inspect")==0 && argc==3)return inspect_or_verify(argv[2],0);
    if(strcmp(argv[1],"verify")==0 && argc==3)return inspect_or_verify(argv[2],1);
    if(strcmp(argv[1],"check-update")==0){
        if(argc<5){usage(stderr);return 2;}
        const char *current=NULL;int allow=0;
        for(int i=3;i<argc;i++){
            if(strcmp(argv[i],"--current")==0 && i+1<argc)current=argv[++i];
            else if(strcmp(argv[i],"--allow-downgrade")==0)allow=1;
            else {usage(stderr);return 2;}
        }
        if(!current){usage(stderr);return 2;}
        return check_update_policy(argv[2],current,allow);
    }
    if(strcmp(argv[1],"help")==0 || strcmp(argv[1],"--help")==0){usage(stdout);return 0;}
    usage(stderr);return 2;
}
