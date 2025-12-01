// decoder_final2.c
// git test003
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAX_CODEWORD_LEN 32

// CSV -> symbol，支援雙引號和轉義
static unsigned char csv_to_sym(const char *s) {
    size_t len = strlen(s);
    if (len >= 2 && s[0]=='\"' && s[len-1]=='\"') {
        // 去掉外層雙引號，處理內部 "" -> "
        char buf[16];
        size_t k=0;
        for (size_t i=1; i<len-1; i++) {
            if (s[i]=='\"' && s[i+1]=='\"') { buf[k++]='\"'; i++; }
            else buf[k++]=s[i];
        }
        buf[k]=0;
        return (unsigned char)buf[0]; // 符號只取第一個字元
    }
    if (s[0]=='\\') {
        if (s[1]=='n') return '\n';
        if (s[1]=='r') return '\r';
        if (s[1]=='t') return '\t';
        if (s[1]=='\\') return '\\';
        if (s[1]=='x') {
            unsigned int v; sscanf(s+2,"%2x",&v);
            return (unsigned char)v;
        }
    }
    return (unsigned char)s[0];
}

// 去掉 codeword 前後空白
static void trim_cw(char *cw) {
    char *start = cw;
    while(*start==' ' || *start=='\t') start++;
    char *end = cw + strlen(cw) - 1;
    while(end > start && (*end==' ' || *end=='\t')) *end--=0;
    if(start != cw) memmove(cw,start,strlen(start)+1);
}

int main(int argc,char **argv){
    if(argc!=4){fprintf(stderr,"Usage: %s out codebook enc\n",argv[0]);return 1;}

    FILE *fcb=fopen(argv[2],"r");
    if(!fcb){perror("codebook");return 1;}

    unsigned char table[1<<16]={0};
    int L=-1;
    char line[512];
    while(fgets(line,sizeof(line),fcb)){
        char *q1=strchr(line,'\"');
        if(!q1) continue;
        char *q2=strchr(q1+1,'\"');
        if(!q2) continue;
        char symbuf[16]; int l=q2-q1-1; if(l>=16) l=15;
        strncpy(symbuf,q1+1,l); symbuf[l]=0;

        char *lastq=strrchr(line,'\"'); if(!lastq||lastq==q2) continue;
        char *open=lastq-1; while(open>line && *open!='"') open--; open++;
        int cwlen = lastq-open; if(cwlen>=MAX_CODEWORD_LEN) cwlen=MAX_CODEWORD_LEN-1;
        char cw[MAX_CODEWORD_LEN]; strncpy(cw,open,cwlen); cw[cwlen]=0;
        trim_cw(cw);

        if(L<0) L = strlen(cw);
        uint32_t val=0;
        for(int i=0;i<L;i++) val=(val<<1)|(cw[i]=='1');
        if(val >= (1u<<L)){fprintf(stderr,"Error: code index overflow\n"); return 1;}
        table[val] = csv_to_sym(symbuf);
    }
    fclose(fcb);

    uint32_t eof=(1u<<L)-1;

    FILE *fenc=fopen(argv[3],"rb");
    if(!fenc){perror("enc");return 1;}
    FILE *fout=fopen(argv[1],"wb");
    if(!fout){perror("out");fclose(fenc);return 1;}

    uint32_t cur=0; int bits=0;
    int b;
    while((b=fgetc(fenc))!=EOF){
        for(int i=7;i>=0;i--){
            cur = (cur<<1)|((b>>i)&1);
            bits++;
            if(bits==L){
                if(cur==eof) goto done;
                unsigned char c = table[cur];
                fputc(c,fout);
                cur=0; bits=0;
            }
        }
    }

done:
    fclose(fenc); fclose(fout);
    return 0;
}
