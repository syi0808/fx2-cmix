#ifndef PREPR_H 
#define PREPR_H 

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#ifndef PHDA9_SCHEMA_RECORD
#define PHDA9_SCHEMA_RECORD 0
#endif

typedef unsigned char  U8;
typedef unsigned short U16;
typedef unsigned int   U32;
typedef uint64_t       U64;

void setpos(FILE *file, U64 newpos) { fseeko(file, newpos, SEEK_SET); }
U64 curpos(FILE *file) { return ftello(file); }
U64 blockread(FILE *file,U8 *ptr, U64 count) {return fread(ptr,1,count,file);}
U64 blockwrite(FILE *file,U8 *ptr, U64 count) {return fwrite(ptr,1,count,file);}

FILE* tmpfile2(void){
    FILE *f;
#ifdef WINDOWS  
    int i;
    char temppath[MAX_PATH]; 
    char filename[MAX_PATH];
    
    //i=GetTempPath(MAX_PATH,temppath);          //store temp file in system temp path
    i=GetModuleFileName(NULL,temppath,MAX_PATH); //store temp file in program folder
    if ((i==0) || (i>MAX_PATH)) return NULL;
    char *p=strrchr(temppath, '\\');
    if (p==0) return NULL;
    p++;*p=0;
    if (GetTempFileName(temppath,"tmp",0,filename)==0) return NULL;
    f=fopen(filename,"w+bTD");
    if (f==NULL) unlink(filename);
    return f;
#else
    f=tmpfile();  // temporary file
    if (!f) return NULL;
    return f;
#endif
}

// WIT
typedef enum {FDECOMPRESS, FCOMPARE, FDISCARD} FMode;
const char UTF8bytes[256] = {
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, 
    0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, 
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, 
    3,3,3,3,3,3,3,3,4,4,4,4,5,5,5,5
};

// utf8 len
int utf8len(char *s){
    return UTF8bytes[(U32)(U8)s[0]] + 1;
}

int wfgets(char *str, int count, FILE  *fp) {
    int c, i = 0;
    while (i<count-1 && ((c=getc(fp))!=EOF)) {
        str[i++]=c;
        if (c=='\n')
            break;
    }
    str[i]=0;
    return i;
}

void wfputs(const char *str,FILE *fp) {
    for (int i = 0; *str; str++){
        putc(*str,fp);
    }
}
// UTF8 to WC (USC) and reverse
int utf8towc(char *dest, U32 ch) {
    int val=0;
    if (ch==1) {
        val=(char)dest[0] ;
        return val;
    }
    if (ch==2) {
        val=dest[1]&0x3F;
        val=val|(dest[0]&0x1F)<<6;
        return val;
    }
    if (ch==3) {
        val|=(dest[0]&0x1F)<<12;
        val|=(dest[1]&0x3F)<<6;
        val=val|(dest[2]&0x3F);
        return val;
    }
    if (ch==4) {
        val|=(dest[0]&0xF)<<18;
        val|=(dest[1]&0x3F)<<12;
        val|=(dest[2]&0x3F)<<6;
        val=val|(dest[3]&0x3F);
        return val;
    }
    return 0;
}
int wctoutf8(char *dest, U32 ch){
    if (ch < 0x80) {
        dest[0] = (char)ch;
        return 1;
    }
    if (ch < 0x800) {
        dest[0] = (ch>>6) | 0xC0;
        dest[1] = (ch & 0x3F) | 0x80;
        return 2;
    }
    if (ch < 0x10000) {
        dest[0] = (ch>>12) | 0xE0;
        dest[1] = ((ch>>6) & 0x3F) | 0x80;
        dest[2] = (ch & 0x3F) | 0x80;
        return 3;
    }
    if (ch < 0x110000) {
        dest[0] = (ch>>18) | 0xF0;
        dest[1] = ((ch>>12) & 0x3F) | 0x80;
        dest[2] = ((ch>>6) & 0x3F) | 0x80;
        dest[3] = (ch & 0x3F) | 0x80;
        return 4;
    }
    return 0;
}

// get num string length terminated by ; or non number
int numlen(char *str) {
    int c, i = 0;
    for (i = 0; *str!=';'; str++){
        if (*str<'0' || *str>'9') return 0;
        i++;
    }   
    return i;
} 
// reduce
//re3
void hent1(char *in,char *out){
    int j, k;
    do {
    j=*in++; *out++=j;
    if (j=='&') {
        k=*in++;
        if (k=='&')  *(int*)out=0x3B706D61, out+=4;  else//';pma'
        if (k=='"')  *(int*)out=0x746F7571, out+=4, *out++=';';  else//'touq'
        if (k=='<')  *(int*)out=0x203B746C, out+=3;  else//' ;tl'
        if (k=='>')  *(int*)out=0x203B7467, out+=3;//' ;tg'
        else   *out++=k;
    }
  }
  while (j!=0);
}
// reverse html entity numbers
void hent6(char *in,char *out){
    int j, k;
    do {
    j=*in++; *out++=j;

    if (j=='&') {
        k=*in++;
        if (k==5){// escape char
            k=utf8len(in);
            int nu=utf8towc(in,k);
            *out='&';
            sprintf(out+1, "#%d;",   nu);
            int a=numlen(out+1+1);
            out=out+2+a+1;
            in=in+k;
        }     
        else   *out++=k;
    }
  }
  while (j!=0);
}
//re4
void hent3(char *in,char *out) {
    int j, k;
    do {
    j=*in++; *out++=j;
    if (j==';' && *(int*)(in-5)==0x706D6126) { //'pma&'
      k = *in++;
      if (k==3); else
      if (k=='"')  *(int*)out=0x746F7571, out+=4, *out++=';';  else//'touq'
      if (k=='<')  *(int*)out=0x203B746C, out+=3;  else//' ;tl'
      if (k=='>')  *(int*)out=0x203B7467, out+=3;  else//' ;tg'
      if (k=='!')  *(int*)out=0x7073626E, out+=4, *out++=';';  else//'psbn'
      if (k=='*')  *(int*)out=0x7361646E, out+=4, *out++='h', *out++=';'; else//'sadn'
      if (k=='^')  *(int*)out=0x7361646D, out+=4, *out++='h', *out++=';'; else //'sadm'
      //if (U8(k)==0xc2 && U8(*(in))==0xae ) {  *(int*)out=0x3B676572, out+=4,in++;}else //'reg'    
      if (U8(k)==0xc2 && U8(*(in))==0xb0 ) {  *(int*)out=0x3B676564, out+=4,in++;}else //'deg'  old=enwik8/9 only
      //if (U8(k)==0xc2 && U8(*(in))==0xb2 ) {  *(int*)out=0x32707573, out+=4,*out++=';',in++;}else //'sup2'  // old    
      //if (U8(k)==0xc2 && U8(*(in))==0xb3 ) {  *(int*)out=0x33707573, out+=4,*out++=';',in++;}else //'sup3'  // old   
      //if (U8(k)==0xe2 && U8(*(in))==0x82&& U8(*(in+1))==0xac ) {  *(int*)out=0x6F727565, out+=4,*out++=';',in+=2;}else //'euro'
      if (U8(k)==0xc3 && U8(*(in))==0x97  ) {  *(int*)out=0x656D6974, out+=4,*out++='s',*out++=';',in++;}//else //'times' old
      //if (U8(k)==0xe2 && U8(*(in))==0x88&& U8(*(in+1))==0x88 ) {  *(int*)out=0x6E697369, out+=4,*out++=';',in+=2;}else //'isin'
      //if (U8(k)==0xe2 && U8(*(in))==0x86&& U8(*(in+1))==0x92 ) {  *(int*)out=0x72726172, out+=4,*out++=';',in+=2;}//else //'rarr'
      //if (U8(k)==0xe2 && U8(*(in))==0x88&& U8(*(in+1))==0x92 ) {  *(int*)out=0x756E696D, out+=4,*out++='s',*out++=';',in+=2;}  //'minus' old
      else *out++=k;
    }
  }
  while (j!=0);
}
//pre3
void hent(char *in,char *out){
    int j, k;
    do {
    j=*in++; *out++=j;

    if (j=='&') {
      k = *(int*)in;
      if (k==0x3B706D61) { //';pma'
       *out++='&', in+=4; k=*(in);
      if ((k=='"') || (k=='<') || (k=='>') || (k=='!') || (k=='*') || (k=='^') ||
      (U8(k)==0xc2 && U8(*(in+1))==0xb2 ) || 
      (U8(k)==0xc2 && U8(*(in+1))==0xb3 ) || 
      (U8(k)==0xc2 && U8(*(in+1))==0xae )  ||
      (U8(k)==0xc2 && U8(*(in+1))==0xb0 )  ||
      (U8(k)==0xe2 && U8(*(in+1))==0x82&& U8(*(in+1+1))==0xac )  ||
      (U8(k)==0xc3 && U8(*(in+1))==0x97  ) ||
      (U8(k)==0xe2 && U8(*(in+1))==0x88&& U8(*(in+1+1))==0x92 )  ||
      (U8(k)==0xe2 && U8(*(in+1))==0x88&& U8(*(in+1+1))==0x88 )  ||
      (U8(k)==0xe2 && U8(*(in+1))==0x86&& U8(*(in+1+1))==0x92 )  
      
      )    *out++=3; }  else  // sup2, escape if char present
      if (k==0x746F7571 && *(in+4)==';')  *out++='"', in+=5;  else//'touq'
      {
        k = k*256 + ' ';
        if (k==0x3B746C20) *out++='<', in+=3;  else//';tl '
        if (k==0x3B746720) *out++='>', in+=3;//';tg '
      }
    }
  }
  while (j!=0);
}
//pre4
void hent2(char *in,char *out){
    int j, k;
    char *start = in;
    do {
    j=*in++; *out++=j;

    if (j=='&' && (in - start > 1 && *(in-2)=='&')) {
        k=*(int*)in;
        if (k==0x746F7571 && *(in+4)==';')  *out++='"', in+=5;  else//'touq'
        if (k==0x7073626E && *(in+4)==';')  *out++='!', in+=5;  else//'psbn'
        if (k==0x7361646E && *(in+4)=='h' && *(in+5)==';')  *out++='*', in+=6;  else//'sadn'
        if (k==0x7361646D && *(in+4)=='h' && *(in+5)==';')  *out++='^', in+=6;  else//'sadm'
        //if (k==0x3B676572  ) { *out++=0xc2,*out++=0xae, in+=4;  }  else//'reg'
        if (k==0x3B676564  ) { *out++=0xc2,*out++=0xb0, in+=4;  }  else//'deg'
        //if (k==0x6F727565  && *(in+4)==';' ) { *out++=0xe2,*out++=0x82,*out++=0xac, in+=5;  }  else//'euro'
        //if (k==0x32707573  && *(in+4)==';' ) { *out++=0xc2,*out++=0xb2, in+=5;  }  else//'sup2'
        //if (k==0x33707573  && *(in+4)==';' ) { *out++=0xc2,*out++=0xb3, in+=5;  }  else//'sup3'
        if (k==0x656D6974  && *(in+4)=='s'&& *(in+5)==';' ) { *out++=0xc3,*out++=0x97, in+=6;  }  else//'times'
        //if (k==0x6E697369  && *(in+4)==';' ) { *out++=0xe2,*out++=0x88,*out++=0x88, in+=5;  }  else//'isin'
        //if (k==0x72726172  && *(in+4)==';' ) { *out++=0xe2,*out++=0x86,*out++=0x92, in+=5;  }  else//'rarr'
        
        //if (k==0x756E696D  && *(in+4)=='s'&& *(in+5)==';' ) { *out++=0xe2,*out++=0x88,*out++=0x92, in+=6;  }  else//'minus'
        {
        k = k*256 + ' ';
        if (k==0x3B746C20)  *out++='<', in+=3;  else//';tl '
        if (k==0x3B746720)  *out++='>', in+=3;//';tg '
        }
    }
  }
  while (j!=0);
}
//Html Entity numbers
//&#
void hent5( char *in,char *out){
    int j, k;
    do {
        j=*in++; *out++=j;
        if (j=='&' && *(in)=='#' && *(in-2)=='&' && *(in+1)>'0'&& *(in+1)<='9') { //  &&#xxx; to &@UTF8
            int n=numlen(in+1);
            int d=atoi(&in[1]);
            if (d>255 && n /*&& n!=38*/){//>2 && n<6
           /* if (d==60) {
                printf("%s",out);
                printf("%s",in);
            }*/
                in++;
                *(out-1)=5;// escape char
                int e=wctoutf8(out,d);
                out=out+e;
                in=in+n+1;
                // printf("Numlen: %d value: %d utflen: %d\n",n,d,e);
            }
        }
  }
  while (j!=0);
}

void hent9( char *in,char *out){
    int j, k;
     int t=0;
  
#define PROCESS(sym, src, dst, CONDITION) \
  {\
     char *t,  *p = src,  *q = dst,  *end = p + strlen(src);\
    while ( (t=strchr(p, sym)) != 0) {   \
        memcpy(q, p, t-p);  q+=t-p;      \
        int count = 0;                   \
        while(*t++ == sym)  ++count;     \
        if ((CONDITION) && (count==1 || count==2))  count = 3-count;\
        memset(q, sym, count);  q+=count;\
        p = t-1;\
    }\
    memcpy(q, p, end-p);  q[end-p]=0; \
  }
   // breaks wordmodel
   PROCESS('{', in, out, 1)
   PROCESS('}', out, in, 1)
    PROCESS('[', in, out, 1)
    PROCESS(']', out, in, 1)
    PROCESS('&', in, out, 1)
    //PROCESS(0x27, out,in, 1)
   // PROCESS('\'', out, in, 1)
   do {
           j=*in++; *out++=j;
        }
        while (j!=0); 


}
void skipline( char *in,char *out ){
    int j;
    do {
        j=*in++; *out++=j;
    }
    while (j!=0);
}
// &" -> "   for now
void removeamp( char *in,char *out ,int skip){
    int j;
    for (int i=0;i<skip;i++) {j=*in++; *out++=j;
    }
    char *outo=out;
    bool amp=false;
     do {
        j=*in++; 
        if (j=='"' || j=='<' || j=='>')  { /*assert(p[-1]=='&');*/  --out; amp=true; }
        *out++=j;
      }
      while (j!=0); 
      //if (amp==true) printf("%s",outo);
}
void restoreamp( char *in,char *out,int skip ){
    int j;
    for (int i=0;i<skip;i++) {j=*in++; *out++=j;
    }
     do {
        j=*in++; 
        if (j=='"' || j=='<' || j=='>')  { /*assert(p[-1]=='&');*/  *out++='&'; }
        *out++=j;
      }
      while (j!=0); 
}

void henttail( char *in,char *out,FILE *o){
    int c, i=0,  j=0;
    static  int lnu=0,f=0, b1=0, lc=0,co=0;
    unsigned char   *ps;
   
    ++lnu;
    j = strlen(in);
    for(i=0; i<j; ++i)  if (*(int*)&in[i]==0x7865743C)  b1 = lnu;//'xet<'
    // parse comment tag
    if (memcmp(&in[6],"<comment>",9)==0 && f==0) co=1;
    for(i=0; i<j; i++) if (*(int*)&in[i]==0x6F632F3C && f==0 && co==1) {//'oc/<'
        co=0,lnu=0,b1=0; // </co mment
        do {
           j=*in++; *out++=j;
        }
        while (j!=0); 
        return;
    }
    if (f==0) {
        
      if (in[0]=='[' && in[1]=='[') {
        ps = (unsigned char*)in + (in[2]==':' ? 1 : 0);
        for (c=2; c<j; ++c)  if ( (in[c]<'a' /*&& in[c]!='-'*/) || in[c]>'z'){
        break;
        } 
         if (c<j && in[c]==':' && !(in[3]==':' || in[2]==':') && co==0) {
            if ((memcmp(&ps[2],"http:",5)==0) ||//(memcmp(&ps[2],"https:",6)==0) ||
              (memcmp(&ps[2],"user:",5)==0) ||// (memcmp(&ps[2],"User:",5)==0) || 
              (memcmp(&ps[2],"media:",6)==0) ||
             // (memcmp(&ps[2],"File:",5)==0) || (memcmp(&ps[2],"file:",5)==0) ||
              (memcmp(&ps[3], "mage:",5)==0) ||
              (memcmp(&ps[3], "ategory:",8)==0) ||
            /*  (memcmp(&ps[3], "iktionary:",10)==0) ||
              (memcmp(&ps[3], "ikipedia:",9)==0) ||
              (memcmp(&ps[2],"Kategoria:",10)==0) ||
              (memcmp(&ps[7],     "gorie:",6)==0) ||
              (memcmp(&ps[2],"imagem:",7)==0) ||
              (memcmp(&ps[2],"wikt:",5)==0) ||
              (memcmp(&ps[2],"Categor",7)==0) ||
              (memcmp(&ps[2],"archivo:",8)==0) ||
              (memcmp(&ps[2],"imagen:",7)==0) ||
              (memcmp(&ps[2],"Archivo:",8)==0) ||
              (memcmp(&ps[2],"Wikiproyecto:",13)==0) ||
              (memcmp(&ps[2],"Utente:",7)==0) || (memcmp(&ps[2],"utente:",7)==0) ||
              (memcmp(&ps[2],":Immagine:",10)==0) ||
              (memcmp(&ps[2],"plik:",5)==0) ||
              (memcmp(&ps[2],"iarchive:",9)==0) ||
              (memcmp(&ps[2],"Datei:",6)==0) ||(memcmp(&ps[2],"datei:",6)==0) ||
              (memcmp(&ps[2],"commons:",8)==0) ||
              (memcmp(&ps[2],"wikisource:",11)==0) ||
              (memcmp(&ps[2],"doi:",4)==0) ||
              (memcmp(&ps[2],"fichier:",8)==0) ||
              (memcmp(&ps[2],"utilisateur:",12)==0) ||
              (memcmp(&ps[2],"hdl:",4)==0) ||
              (memcmp(&ps[2],"irc:",4)==0) ||
              (memcmp(&ps[2],"wikibooks:",10)==0) ||    
              (memcmp(&ps[2],"meta:",5)==0) || 
              (memcmp(&ps[2],"categoria:",10)==0) || 
              (memcmp(&ps[2],"immagine:",9)==0) || 
              (memcmp(&ps[3], "ikipedysta:",11)==0) || 
              (memcmp(&ps[2],"wikia:",6)==0) || 
              (memcmp(&ps[2],"incubator:",10)==0) || 
              (memcmp(&ps[2],"ficheiro:",9)==0) || (memcmp(&ps[2],"Ficheiro:",9)==0) ||
              (memcmp(&ps[2],"arquivo:",8)==0) ||
              (memcmp(&ps[2],"foundation:",11)==0) ||
              (memcmp(&ps[2],"template:",9)==0) ||
              (memcmp(&ps[2],"wikinews:",9)==0) ||
              (memcmp(&ps[2],"bild:",5)==0) ||*/
              (memcmp(&ps[2],"r:Wikipédia:Aide]]",20)==0) ||
              (memcmp(&ps[2],"de:Boogie Down Produ",20)==0) ||
              (memcmp(&ps[2],"da:Wikipedia:Hvordan",20)==0) ||
              (memcmp(&ps[2],"sv:Indiska musikinstrument",26)==0) ||
             /* (memcmp(&ps[2],"es:Coronel Sanders",18)==0) ||
             ((memcmp(&ps[2],"fr:Wikip",8)==0)  &&  (memcmp(&ps[10+2],"dia:Aide",8)==0) ) ||
              (memcmp(&ps[2],"pt:Wikipedia:Artigos cu",23)==0) ||*/
              (lnu-b1<4) ){
                // skip if not lang at end
                do {
                 j=*in++; *out++=j;
                 }
                 while (j!=0); 
                 return;
            }
            f=1, lc=0;
            ++lc;
            for(i=0; i<j; i++)  if (*(int*)&in[i]==0x65742F3C && (*(in+4+i+2)=='>'))  f=0,lnu=0,b1=0;//'et/<'
            hent9(in,out);
            wfputs(in,o);
            out[0]=0;
            return;
        }
      }
      do {
        j=*in++; *out++=j;
      }
      while (j!=0); 
      return;
    }
    else if (f==1){
     ++lc;
     for(i=0; i<j; i++)  if (*(int*)&in[i]==0x65742F3C)  f=0,lnu=0,b1=0;//'et/<'
     hent9(in,out);
     wfputs(in,o);
     out[0]=0;
    }
}

void henttail1( char *in,char *out,FILE *o,char *p2){
    int i, j, k;
    static int c=0 ,lnu=0, f=0;
    static  char *p4=p2;
    char su[8192*8];
    char *s=su;
    char ou[8192*8];
    ++lnu;
    j = strlen(in);
    for(i=0; i<j; i++) if (*(int*)&in[i]==0x7865743C) c=1, f=lnu;//'xet<'
    for(i=0; i<j; i++) if (*(int*)&in[i]==0x65742F3C && (*(in+4+i+2)=='>' )) c=0; //'et/<' </te xt>

    if ((memcmp(&in[j-12],"</revision>",11)==0)&& c==1 && (lnu-f>=4) /*&& *p4!=0&& (p4-p2)<size*/) {
        c=0;
        do {
          k=(int)(strchr(p4,10)+1-(char*)p4);
          for (int i = 0; i<k;i++ ){
            su[i]=*p4++;
          }
          su[k]=0;
          // parse line 
          hent9(s,ou);
          if (!(strstr(s, "</text>") || strstr(s, "</revision>")|| strstr(s, "</page>")/*|| strstr(s, "</sha1>")*/)) {
              restoreamp(s,ou,0);
              skipline(ou,s);
          } 
          
          hent6( s,ou);
          hent1( ou,s);
          hent3( s,ou);
          
          wfputs(ou,o);
        }
        while (memcmp(&p4[-8],"</text>",7) );
        wfputs(in,o); // out current line
        out[0]=0;
    }else{
        do {
        j=*in++; *out++=j;
      }
      while (j!=0); 
    }
}

// above needs global vars to remove some functions (less code)
// 
// wit restore
void decode_txt_wit(FILE*in,  FILE*out1,U64 size){
    char s[8192*8];
    char o[8192*8];
    int i, j, f = 0, lastID = 0,tf=0;
#if PHDA9_SCHEMA_RECORD
    int lastRevisionID = 0, lastContributorID = 0, lastParentID = 0;
#endif
    wfgets(s, 22, in);    
    U64 winfo=atoi(&s[0]);
    U64 insize=ftello(in);
    U64 tsize=size-winfo; // winfo <- tail lenght
    setpos(in,tsize); // tail data pos
    // header
    wfgets(s, 16, in);    
    int headerlenght=atoi(&s[0]);
    char *h1=(char*)calloc(headerlenght+1,1);
    char *h1p=h1;
    // lang
    wfgets(s, 16, in);    
    int langlenght=atoi(&s[0]);
    char *p1=(char*)calloc(langlenght+1,1);
 
    if(headerlenght)  blockread(in,(U8*)h1,U64(headerlenght));  //read header
    if(langlenght)    blockread(in,(U8*)p1,U64(langlenght));  //read lang
    
    setpos(in,insize+1);
    int header=0;
    do {
        j=wfgets(s, 8192*8, in);    

        if (curpos(in) > tsize ) {
            j=j-(curpos(in)-tsize); // cut tail
            s[j]=0;
        }
        
        if (header==1){
            int n=0;
            int cont=0;
#if PHDA9_SCHEMA_RECORD
            int recordDone=0;
#endif
            do {
                *(int*)o = 0x20202020;
                j=4;
                //'ider' 'iver' 'tser'
                if (*(int*)&h1p[0]!=0x69646572 &&*(int*)&h1p[0]!=0x69766572&& *(int*)&h1p[0]!=0x74736572 && n!=0) *(int*)(o+j)= 0x20202020,j=j+2;
                if (cont==1) *(int*)(o+j)= 0x20202020,j=j+2;
                if (*(int*)&h1p[0]==0x6E6F632F) cont=0;//'noc/'
                int k=(int)(strchr(h1p,10)+1-(char*)h1p);

                // id
                if ( n==0){
                    if ( (*(int*)&h1p[0]&0xffffff)==0x3E736E){//'>sn'
                        o[j]='<';
                        memcpy(o+j+1, h1p, k);
                        int e=(strchr(h1p, '>')-h1p)+2;
                        if (e!=k || cont==1) { //end tag
                            o[k+j++]='<';
                            o[k+j++]='/';
                            memcpy(o+j+k, h1p, k);
                            j=j+(strchr(h1p, '>')-h1p)+1;
                        }
                        o[k+j++]=10;
                        o[k+j]=0;
                        wfputs(o,out1);
                    } else{

                        n++;
                        lastID = lastID+ atoi(&h1p[1]);
                        sprintf(o+j, "<id>%d</id>%c",   lastID, 10);
                        wfputs(o,out1);
                    }
                }
#if PHDA9_SCHEMA_RECORD
                else if (h1p[0]=='R' && (h1p[1]=='-' || (h1p[1]>='0' && h1p[1]<='9'))){
                    lastRevisionID += atoi(h1p+1);
                    sprintf(o, "    <revision>%c      <id>%d</id>%c",
                        10, lastRevisionID, 10);
                    wfputs(o,out1);
                }
                else if (h1p[0]=='A' && (h1p[1]=='-' || (h1p[1]>='0' && h1p[1]<='9'))){
                    lastParentID += atoi(h1p+1);
                    sprintf(o, "      <parentid>%d</parentid>%c",
                        lastParentID, 10);
                    wfputs(o,out1);
                }
                else if (h1p[0]=='T'){
                    char *p = strchr(h1p, ':');
                    int d = atoi(h1p+3), hms = atoi(p+1), h = hms/3600;
                    o[0]=h1p[1],o[1]=h1p[2],o[2]=' ';
                    int y=atoi(&o[0]);
                    sprintf(o, "      <timestamp>%d-%02d-%02dT%02d:%02d:%02dZ</timestamp>%c",
                    y + 2001, d/31+1, d%31+1, h, hms/60 - h*60, hms%60, 10);
                    wfputs(o,out1);
                }
                else if (h1p[0]=='U' || h1p[0]=='P'){
                    char lineEnd=h1p[k-1];
                    h1p[k-1]=0;
                    sprintf(o, "      <contributor>%c        <%s>%s</%s>%c",
                        10, h1p[0]=='U' ? "username" : "ip", h1p+1,
                        h1p[0]=='U' ? "username" : "ip", 10);
                    h1p[k-1]=lineEnd;
                    wfputs(o,out1);
                    cont=1;
                }
                else if (h1p[0]=='I' && (h1p[1]=='-' || (h1p[1]>='0' && h1p[1]<='9'))){
                    lastContributorID += atoi(h1p+1);
                    sprintf(o, "        <id>%d</id>%c", lastContributorID, 10);
                    wfputs(o,out1);
                }
                else if (h1p[0]=='E' && h1p[1]==10){
                    wfputs("      </contributor>\n",out1);
                    cont=0;
                    recordDone=1;
                }
#endif
                else if (*(int*)&h1p[0]==0x656D6974 ){//'emit'
                    char *p = strchr(h1p, ':');
                    int d = atoi(&h1p[19-7]), hms = atoi(p+1), h = hms/3600;
                    o[0]=h1p[17-7],o[1]=h1p[18-7],o[2]=' ';
                    int y=atoi(&o[0]);
                    sprintf(o, "      <timestamp>%d-%02d-%02dT%02d:%02d:%02dZ</timestamp>%c",
                    y + 2001, d/31+1, d%31+1, h, hms/60 - h*60, hms%60, 10);
                    wfputs(o,out1);
                }
                else if (n || cont==1){
                    o[j]='<';
                    memcpy(o+j+1, h1p, k);
                    int e=(strchr(h1p, '>')-h1p)+2;
                    if (e!=k || cont==1) { //end tag
                        o[k+j++]='<';
                        o[k+j++]='/';
                        memcpy(o+j+k, h1p, k);
                        j=j+(strchr(h1p, '>')-h1p)+1;
                        
                    }
                    o[k+j++]=10;
                    o[k+j]=0;
                    wfputs(o,out1);
                    if (*(int*)&h1p[0]==0x746E6F63) cont=1;//'tnoc'
                }
                h1p=h1p+k;
#if PHDA9_SCHEMA_RECORD
                if (recordDone) break;
#endif
                if (memcmp(&h1p[0],"contributor dele",16)==0)break;
            }
            while (memcmp(&h1p[0],"/contributor>",13)    );
#if PHDA9_SCHEMA_RECORD
            if (!recordDone) {
#endif
                *(int*)o = 0x20202020;
                j=4;
                *(int*)(o+j)= 0x20202020,j=j+2;
                int k=(int)(strchr(h1p,10)+1-(char*)h1p);
                o[j]='<';
                memcpy(o+j+1, h1p, k);
                o[k+j++]=10;
                o[k+j]=0;
                wfputs(o,out1);
                h1p=h1p+k;
#if PHDA9_SCHEMA_RECORD
            }
#endif
            header=0;
        }

        {
            if (tf==0 && memcmp(&s[j-9],"</title>",8)==0 && *(int*)s==0x20202020) {
            header=1;

            }
        }

#if PHDA9_SCHEMA_RECORD
            if (strcmp(s, "\1m\n")==0) {
                wfputs("      <minor />\n",out1);
                continue;
            }
            if (strcmp(s, "\1M\n")==0) {
                wfputs("      <model>wikitext</model>\n",out1);
                continue;
            }
            if (strcmp(s, "\1F\n")==0) {
                wfputs("      <format>text/x-wiki</format>\n",out1);
                continue;
            }
#endif

            skipline(s,o);// remove this
            hent9(o,s);
            henttail1(o,s,out1,p1);
            if (s[0]==0) {   continue;       }
            
            int skip=0;
            char *p = strstr(s, "<text ");//, *w;
            if (p)  { tf=1, p = strchr(p, '>');   skip= (char*)p+1-(char*)s; 
                if(p[-1]=='/' /*|| p[2]==0*/) tf=0;  
                
            }
            
            if (strstr(s, "</text>") || strstr(s, "</revision>")|| strstr(s, "</page>")/*|| strstr(s, "</sha1>")*/)  tf=0;
            if (tf) {
                restoreamp(s,o,skip);
                skipline(o,s);
            }
            hent6( s,o);
            hent1( o,s);
            hent3( s,o);
            wfputs(o,out1);
       
    }
    while (curpos(in) < tsize);
    if(headerlenght)free(h1);
    if(langlenght)  free(p1);
   
}

void encode_txt_wit(FILE* in, FILE* out) {
    char s[8192*8];
    char o[8192*8];
    FILE *out1=tmpfile2(); // lang
    FILE *out3=tmpfile2(); // header

    for (int i = 0; i<21; i++){
        putc(32,out);
    }
    putc('\n',out);
    int i, j, f = 0, lastID = 0,tf=0;
#if PHDA9_SCHEMA_RECORD
    int lastRevisionID = 0, lastContributorID = 0, lastParentID = 0;
    bool revisionIDPending=false;
#endif

  do {
    j=wfgets(s, 8192*8, in);

    if (f==2) {
        if (*(int*)&s[4]==0x3E736E3C) { // ns '>sn<'
            char *p =strchr(s, '>');
            if (p) {
                p = strchr(p+1, '<');
                if (p)  p[0] = 10,  p[1] = 0;
            }
            if (s[0]!=' ' ||s[1]!=' ' ||s[2]!=' ' ||s[3]!=' '){
                printf("FAIL:\n%s",s);
                exit(1);// just fail
                 }
            wfputs(s+5,out3);
            continue;
        }
        // id
        int curID = atoi(&s[8]);
        if (*(int*)&s[4] != 0x3E64693C){ 
        printf("FAIL:\n%s",s);
        exit(1);
        }//return 0;// just fail it '>di<'
        //if (curID <= lastID)        return 0;
       sprintf(o,  ">%d%c", curID - lastID, 10);
        wfputs(o,out3);
        lastID = curID;
#if PHDA9_SCHEMA_RECORD
        revisionIDPending=true;
#endif
        f = 1;
        continue;
    }
    if (f) {
#if PHDA9_SCHEMA_RECORD
        if (revisionIDPending && strcmp(s, "    <revision>\n")==0) {
            continue;
        }
        if (revisionIDPending && memcmp(s, "      <id>", 10)==0) {
            int revisionID=atoi(s+10);
            sprintf(o, "R%d%c", revisionID-lastRevisionID, 10);
            wfputs(o,out3);
            lastRevisionID=revisionID;
            revisionIDPending=false;
            continue;
        }
        if (f==1 && memcmp(s, "      <parentid>", 16)==0) {
            int parentID=atoi(s+16);
            sprintf(o, "A%d%c", parentID-lastParentID, 10);
            wfputs(o,out3);
            lastParentID=parentID;
            continue;
        }
#endif
        if (*(int*)&s[6]==0x6D69743C) {//'mit<'
            int year   = atoi(&s[17]);
            int month  = atoi(&s[22]);
            int day    = atoi(&s[25]);
            int hour   = atoi(&s[28]);
            int minute = atoi(&s[31]);
            int second = atoi(&s[34]);
#if PHDA9_SCHEMA_RECORD
            sprintf(o, "T%02d%d:%d%c",
#else
            sprintf(o, "timestamp>%02d%d:%d%c",
#endif
                    year-2001, month*31+day-32, hour*3600+minute*60+second, 10);
            wfputs(o,out3);
            continue;
        }
#if PHDA9_SCHEMA_RECORD
        if (f==1 && strcmp(s, "      <contributor>\n")==0) {
            f=3;
            continue;
        }
        if (f==3 && memcmp(s, "        <username>", 18)==0) {
            char *end=strstr(s+18, "</username>");
            if (end!=0) {
                *end=0;
                sprintf(o, "U%s%c", s+18, 10);
                wfputs(o,out3);
                continue;
            }
        }
        if (f==3 && memcmp(s, "        <ip>", 12)==0) {
            char *end=strstr(s+12, "</ip>");
            if (end!=0) {
                *end=0;
                sprintf(o, "P%s%c", s+12, 10);
                wfputs(o,out3);
                continue;
            }
        }
        if (f==3 && memcmp(s, "        <id>", 12)==0) {
            int contributorID=atoi(s+12);
            sprintf(o, "I%d%c", contributorID-lastContributorID, 10);
            wfputs(o,out3);
            lastContributorID=contributorID;
            continue;
        }
        if (f==3 && strcmp(s, "      </contributor>\n")==0) {
            wfputs("E\n",out3);
            f=0;
            continue;
        }
#endif
         char *p =strchr(s, '>');
        if (p) {
            p = strchr(p+1, '<');
            if (p)  p[0] = 10,  p[1] = 0;
        }
        if (s[0]!=' ' ||s[1]!=' ' ||s[2]!=' ' ||s[3]!=' ') {
            printf("FAIL:\n%s",s);
          exit(1);// just fail
           }
        int s2=0; //lenght
        if (f==3) {
            if (*(int*)&s[6]==0x6F632F3C)  s2=7;//'oc/<'
            else                       s2=9;
        }
        else {
          if (*(int*)&s[4]==0x7665723C || *(int*)&s[4]==0x7365723C|| *(int*)&s[4]==0x6465723C) s2=5;//'ver<' 'ser<' 'der<'
          else                                             s2=7;
          if (*(int*)&s[6]==0x6E6F633C) {//'noc<'
           f=3;
           if (f==3 && *(int*)&s[6+4+4+4]==0x6C656420)  f=0; //'led 'special case "deleted"
          }
        }
        if (s2){
            wfputs(s+s2,out3);
        }
    }
    else  {
#if PHDA9_SCHEMA_RECORD
        if (strcmp(s, "      <minor />\n")==0) {
            wfputs("\1m\n",out);
            continue;
        }
        if (strcmp(s, "      <model>wikitext</model>\n")==0) {
            wfputs("\1M\n",out);
            continue;
        }
        if (strcmp(s, "      <format>text/x-wiki</format>\n")==0) {
            wfputs("\1F\n",out);
            continue;
        }
#endif
        hent(s,o);
        hent2(o,s);
        hent5(s,o);
        
        int skip=0;
        char *p = strstr(o, "<text ");
        if (p)  { tf=1, p = strchr(p, '>');  skip= (char*)p+1-(char*)o;
        if(p[-1]=='/' /*|| p[2]==0*/) tf=0;  

        }
        
        if (strstr(o, "</text>") || strstr(o, "</revision>")|| strstr(o, "</page>"))  tf=0;
        if (tf) {
        removeamp(o,s,skip);
        skipline(s,o);
        
        }
        henttail(o,s,out1);
        
        skipline(s,o);

        hent9(o,s);
        wfputs(o,out);
}
    if (tf==0) for(i=0; i<j; i++)
      if (*(int*)&s[i]==0x69742F3C && *(int*)&s[i+4]==0x3E656C74&&*(int*)s==0x20202020) {f=2;//'it/<' '>elt'

  }
    for(i=0; i<j; i++)
      if (*(int*)&s[i]==0x6F632F3C && *(int*)&s[i+4]==0x6972746E) f=0;//'oc/<' 'irtn'
  }
  while (!feof(in));
  // output tail to main file and report tail size as info
  U64 msize=curpos(out);
  int tsize=curpos(out1);
  int headersize=curpos(out3);
   setpos(out1,0);
   setpos(out3,0);
   sprintf(o, "%d%c", headersize, 10);
   j=strlen(o);
   wfputs(o,out); //header
   sprintf(o, "%d%c", tsize, 10);
   j=j+strlen(o);
   wfputs(o,out); //lang

   for(U64 i=0; i<headersize; i++) {
       int a=getc(out3);
       putc(a,out);
   }
   
   for(U64 i=0; i<tsize; i++) {
       int a=getc(out1);
       putc(a,out);
   }
   

   fclose(out3);
   fclose(out1);
 /* printf("Main size: %d kb\n",U32(msize/1024));
  printf("header size: %d kb\n",U32(headersize/1024));
  printf("Langs size: %d kb\n",U32(tsize/1024));
  */
  tsize=tsize+j+headersize;
  // write out size of tail lenght
  setpos(out,0);
  sprintf(o, "%d%c", tsize, 20);
  for (int i = 0; i<20; i++){
       U8 a=o[i];
       if (a>='0' && a<='9') putc(a,out);
       else break;
  }

}
// end WIT


bool cat(char const * filename_from1, char const * filename_from2, char const * filename_to) {
  FILE* ifile1 = fopen(filename_from1, "rb");
  FILE* ifile2 = fopen(filename_from2, "rb");
  FILE* ofile = fopen(filename_to, "wb");
  
  do {
    int c=getc(ifile1);
    if (c==EOF) break;
    putc(c,ofile);
  }
  while (!feof(ifile1));
  do {
    int c=getc(ifile2);
    if (c==EOF) break;
    putc(c,ofile);
  }
  while (!feof(ifile2));
  fclose(ifile1);
  fclose(ifile2);
  fclose(ofile);

  return true;
}

int phda9_prepr() {
   // open files
  FILE *in=fopen(".main_reordered", "rb");
  //if (!in)  exit(1);
  FILE *out=fopen(".main_phda9prepr", "wb");
  //if (!out)  exit(1);

  // process file
  encode_txt_wit(in,out);
  fclose(in) ;
  fclose(out);
  return 0;
}

int phda9_resto() {
    
  FILE *in=fopen(".main_decomp", "rb");
  //if (!in)  exit(1);
  FILE *out=fopen(".main_decomp_restored", "wb");
  //if (!out)  exit(1);

  // get size
  fseek(in, 0, SEEK_END);
  U64 insize=ftell(in);
  fseek(in, 0, SEEK_SET);
  // process file
  decode_txt_wit(in,out,insize);
  fclose(in) ;
  fclose(out);
  return 0;
}
#endif // PREPR_H
