//this is for encoder
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h> // for log2

#define BYTE_MAX     256  //maximum one byte number
#define MAX_SYMB     3000  //maximux symbol type

typedef struct Symb{
    unsigned char chr[4];     //bytes of symbol
    int useLen;               //size: 1~4 bytes
    int count;                //number of this symbol
    double prob;              //probability 
    char code[256];           //fixed encode

    struct Symb *left;        // left child
    struct Symb *right;       // right child
    struct Symb *parent;      // root parent
    int is_leaf;              // is leaf node
} Symb;

unsigned char byteBuffer = 0; // byte buffer for output (8 bits)
int bitInBuff = 0; //number of bits in buffer

//------------------ utf-8 decoding ------------------ 
static int utf8_len(unsigned char b0){  //use first byte to check UTF-8 using length
    if ((b0 & 0x80)==0x00) return 1; //0xxxxxxx
    if ((b0 & 0xE0)==0xC0) return 2; //110xxxxx
    if ((b0 & 0xF0)==0xE0) return 3; //1110xxxx
    if ((b0 & 0xF8)==0xF0) return 4; //11110xxx
    return 0;  
}
static int is_utf8_follow(unsigned char b){ //check is UTF-8 follow byte legal (10xxxxxx)
    return (b & 0xC0)==0x80; //10xxxxxx
}

//------------------ big-5 decoding ------------------ 
static int big5_len(unsigned char b0){
    if (b0 <= 0x7F) return 1;                 // first byte < 127 ->ASCII
    if (b0 >= 0x81 && b0 <= 0xFE) return 2;   // first byte is big-5 high byte 0x81-0xFE
    return 1;                                 
}
static int is_big5_follow(unsigned char b1){
    // big5 low byte 0x40-0x7E , 0xA1-0xFE
    return ( (b1 >= 0x40 && b1 <= 0x7E) || (b1 >= 0xA1 && b1 <= 0xFE) ); //check is big-5 rule
}

// -------------- qsort compare function for codebook --------------
static int cmp_codebook(const void *a, const void *b){
    // x, y are Symb pointers ; a, b are pointers to Symb pointers
    const Symb *x = *(const Symb**)a; 
    const Symb *y = *(const Symb**)b;
    // Primary key: symbol count (ascending)
    if(x->count != y->count) 
        return x->count - y->count;
    // Secondary key: symbol byte length (ascending)
    if(x->useLen != y->useLen) 
        return x->useLen - y->useLen;
    return memcmp(x->chr, y->chr, x->useLen); // Tertiary key: byte index (ascending)
}


// -------------- find two minimum count nodes --------------
void find_two_min(Symb *nodes[], int n, int *min1, int *min2) {
    int m1 = -1, m2 = -1; // store minimum counts
    *min1 = -1; 
    *min2 = -1;

    for (int i = 0; i < n; i++) { // 
        // if this node has a parent, skip it
        if (nodes[i]->parent != NULL) continue;

        // find smaller count
        if (m1 == -1 || nodes[i]->count < m1) {
            m2 = m1;       // shift current minimum to second minimum
            *min2 = *min1;
            
            m1 = nodes[i]->count; // update minimum
            *min1 = i;
        }
        // find second smaller count
        else if (m2 == -1 || nodes[i]->count < m2) {
            m2 = nodes[i]->count; // update second minimum
            *min2 = i;
        }
    }
}
// -------------- build huffman tree --------------
// nodes[] save all symbol nodes' pointers
// return tree root
Symb* build_huffman_tree(Symb *nodes[], int used_cnt) {
    int n = used_cnt; // current number of nodes in the array
    
    // constantly merge until only one root node 
    // each merge reduces 2 orphans and adds 1 new parent, so total -1, do used_cnt - 1 times
    for (int i = 0; i < used_cnt - 1; i++) {
        int min1, min2;
        find_two_min(nodes, n, &min1, &min2); // find two minimum nodes except those with parents
        
        if (min1 == -1 || min2 == -1) break; // check error

        // build new parent node
        Symb *parent = (Symb*)calloc(1, sizeof(Symb));
        // parent node properties
        parent->left = nodes[min1];   // left is the smaller 
        parent->right = nodes[min2];  // right is the larger
        parent->count = nodes[min1]->count + nodes[min2]->count; // parent count is sum of children
        parent->is_leaf = 0;          // not a symbol, is middle node
        parent->parent = NULL;        // no parent yet

        // set children's parent to new parent node
        nodes[min1]->parent = parent;
        nodes[min2]->parent = parent;

        // add new parent node to nodes[]
        nodes[n] = parent;
        n++;
    }
    return nodes[n-1]; // return tree root 
}
// -------------- generate huffman codes --------------
// node: current node
// current_code: current code string 010...
// depth: current depth in tree
// recursively DFS traverse the tree to generate codes (pre-order)
void generate_codes(Symb *node, char *current_code, int depth) {
    if (node == NULL) return;

    // leaf node, save code
    if (node->is_leaf) {
        current_code[depth] = '\0'; // last character ending
        strcpy(node->code, current_code); // copy current code to node code
        return;
    }
    // start from root
    // go left, code add '0'
    current_code[depth] = '0';
    generate_codes(node->left, current_code, depth + 1); // recursive left

    // go right, code add '1'
    current_code[depth] = '1';
    generate_codes(node->right, current_code, depth + 1); // recursive right
}
// -------------- write char to codebook csv file --------------
static void csv_char(const unsigned char *s, int len, FILE *fp){   
    if (len==1 && s[0]=='\r'){ fputs("\"\\r\"",fp); return; }
    if (len==1 && s[0]=='\n'){ fputs("\"\\n\"",fp); return; }
    if (len==1 && s[0]=='\t'){ fputs("\"\\t\"",fp); return; }
    fputc('"',fp);
    for(int i=0;i<len;i++){
        if (s[i]=='"') fputc('"',fp); // escape double quote
        fputc(s[i],fp); // output symbol
    }
    fputc('"',fp);
}
// -------------- write code to output file --------------
static void write_code(const char *code, int len, FILE *fp){
    for (int k = 0; k < len; k++) {
        // char '0'(48) or '1'(49) to bit 0 or 1
        byteBuffer = (byteBuffer << 1) | (code[k] - '0'); // add 0 or 1 to buffer and shift left
        bitInBuff++;

        // when buffer is full (8 bits), write to file
        if (bitInBuff == 8) {
            //fprintf(stderr, "DEBUG [Data]: 0x%02X\n", g_bitBuffer); // print debug info
            fputc(byteBuffer, fp);
            bitInBuff = 0;
            byteBuffer = 0;
        }
    }
} 

int main(int argc, char *argv[]) {
    // check argument count
    if (argc != 4) {
        fprintf(stderr, "usage: %s in_fn cb_fn enc_fn\n", argv[0]);
        return 1; 
    }
    // open files
    // Read Binary
    FILE *fin = fopen(argv[1], "rb"); 
    if (fin == NULL) { perror(argv[1]); return 1; }
    // Write
    FILE *fcsv = fopen(argv[2], "w"); 
    if (fcsv == NULL) { perror(argv[2]); return 1; }
    // Write Binary
    FILE *fout = fopen(argv[3], "wb"); 
    if (fout == NULL) { perror(argv[3]); return 1; }

    // === symbol statics ===
    Symb symb[MAX_SYMB]={0};
    
    // initial ascii symbols
    for(int i=0;i<=0x7F;i++){ 
        symb[i].chr[0]=(unsigned char)i; 
        symb[i].useLen=1; 
        symb[i].count=0; // start from zero
    }

    int used = BYTE_MAX;  // used symbol types
    int total = 0;        // total symbol count
    int c; // a byte read from file

    // ---------------------- statistic symbol --------------------
    while((c=fgetc(fin))!=EOF){
        unsigned char b0 = (unsigned char)c; 
        // handle ascii 0~127
        if(b0 <= 0x7F){ 
            symb[b0].count++;
            total++;
            continue; // next char
        }

        // handle multibyte symbol (utf-8 / big5)
        int symbLen=1; 
        unsigned char tmp[4]; //store symbol following bytes
        tmp[0]=b0; // first byte
        int uLen=0; // symbol use length (bytes)
        int read=1, ok=0; 

        // try utf-8
        uLen = utf8_len(b0); // check is legal utf-8 first byte & length
        if(uLen > 1){
            ok = 1;
            for(int i=1;i<uLen;i++){
                int d=fgetc(fin);
                if(d==EOF){ ok=0; break; } // end of file break
                tmp[read++]=(unsigned char)d; // store symbol in tmp[] (2~4 bytes)
                if(!is_utf8_follow(tmp[read-1])){ ok=0; break; } // illegal following utf-8 byte
            }
        }
        if (ok){ 
            symbLen = uLen; // save utf-8 length
        } else { 
            for (int j=read-1;j>=1; --j) ungetc(tmp[j], fin); // push back to b0
        }

        // try big-5
        if (symbLen == 1){  // check not utf-8 (symbLen unchanged)
            uLen = big5_len(b0); // check is legal big-5 first byte & length
            if (uLen == 2){ 
                int d = fgetc(fin); // read following byte (second byte)
                if (d != EOF && is_big5_follow((unsigned char)d)){ // legal big-5 follow byte
                    tmp[1] = (unsigned char)d;
                    symbLen = 2;   // big-5 always 2 bytes
                } else {
                    if (d != EOF) ungetc(d, fin); // push back to b0
                }
            }
        }

        // handle non ASCII, UTF-8, Big-5 symbols (128~255)
        if (symbLen == 1){ // symbleLen unchanged -> unknown one byte symbol
            if (symb[b0].useLen == 0){  // first time seen initialize
                symb[b0].useLen = 1;
                symb[b0].chr[0] = b0;
            }
            symb[b0].count++; // count this symbol 
            total++; // count total symbols
            continue;
        }

        // save multibyte symbol (utf-8 / big5)
        int found=0;
        for(int i=BYTE_MAX;i<used;i++){ 
            // find existing symbol
            if(symb[i].useLen==symbLen && memcmp(symb[i].chr,tmp,symbLen)==0){ 
                symb[i].count++; // count this symbol
                found=1; // mark found
                total++;
                break; 
            }
        }

        // not found, add new symbol
        if(!found){ 
            // check symbol limit
            if(used >= MAX_SYMB) { fprintf(stderr, "symbols are too many (%d)!\n", used); return 1; }
            memcpy(symb[used].chr, tmp, symbLen); // copy symbol bytes
            symb[used].useLen = symbLen; // set symbol length
            symb[used].count = 1; // initialize count
            total++; 
            used++; // push back used symbol types
        }
    }

    // ------------------ build huffman tree & generate codebook --------------------
    // add EOF symbol at the end
    if (used < MAX_SYMB) {
        strcpy((char*)symb[used].chr, "EOF"); // symbol "EOF"
        symb[used].useLen = 3;                // length 3
        symb[used].count = 1;                 // count 1
        symb[used].is_leaf = 1;               // leaf node
        symb[used].prob = 0.0;                // probability 0    
        used++; // used symbol types +1
    }

    // prepare nodes array for building huffman tree (count > 0)
    // bigger array to hold all nodes including parents
    Symb *nodes[MAX_SYMB * 2]; 
    int active_cnt = 0; // current active node count

    // collect all symbols with count > 0 to nodes[]
    for(int i = 0; i < used; i++) {
        if(symb[i].count > 0) {
            symb[i].is_leaf = 1;     // symbol is leaf node
            symb[i].left = NULL;     // initialize tree pointers
            symb[i].right = NULL;
            symb[i].parent = NULL;
            
            nodes[active_cnt] = &symb[i]; // 加入名單
            active_cnt++; // push back active count
        }
    }

    // build huffman tree
    Symb *root = build_huffman_tree(nodes, active_cnt);

    // recursively generate codes from huffman tree
    char code_buff[256]; // temporary code buffer for traversal
    generate_codes(root, code_buff, 0);

    // output codebook to csv file
    Symb *sorted_nodes[MAX_SYMB]; // array to hold pointers for sorting
    int output_cnt = 0;

    for(int i = 0; i < used; i++) {
        if (symb[i].count > 0) { // only output symbols with count > 0
            sorted_nodes[output_cnt] = &symb[i]; // copy pointer for sorting
            output_cnt++;
        }
    }

    // sort by count, length, byte index using cmp_codebook
    // sorting sorted_nodes[] array
    qsort(sorted_nodes, output_cnt, sizeof(Symb*), cmp_codebook);

    Symb *eof_symb = &symb[used-1]; 
    fprintf(fcsv, "\"EOF\",0,0.000000000000000,%s,0.000000000000000\n", eof_symb->code);

    // output csv 
    for(int i = 0; i < output_cnt; i++) {
        Symb *s = sorted_nodes[i]; 

        // skip EOF symbol
        if (s == eof_symb) continue;

        // probability
        if (total > 0) s->prob = (double)s->count / total;
        else s->prob = 0.0;
        
        // self-information
        double self_info = 0.0;
        if (s->prob > 0) {
            self_info = -log(s->prob) / log(2.0); 
        }

        // normal output
        csv_char(s->chr, s->useLen, fcsv); 
        fprintf(fcsv, ",%ld,%.15f,%s,%.15f\n", s->count, s->prob, s->code, self_info);
    }
    fclose(fcsv);

    // ------------------ encode input file -----------------------
    rewind(fin); // reset file pointer to beginning
    while((c=fgetc(fin))!=EOF){
        unsigned char b0 = (unsigned char)c; 
        // 1. handle ascii 0~127
        if(b0 <= 0x7F){ 
            // count++ -> write_code
            write_code(symb[b0].code, strlen(symb[b0].code), fout); 
            continue; 
        }

        // 2. handle multibyte symbol (utf-8 / big5) !! same as above !!
        int symbLen=1; 
        unsigned char tmp[4];
        tmp[0]=b0;
        int uLen=0;
        int read=1, ok=0; 

        uLen = utf8_len(b0); 
        if(uLen > 1){
            ok = 1;
            for(int i=1;i<uLen;i++){
                int d=fgetc(fin);
                if(d==EOF){ ok=0; break; } 
                tmp[read++]=(unsigned char)d; 
                if(!is_utf8_follow(tmp[read-1])){ ok=0; break; } 
            }
        }
        if (ok){ 
            symbLen = uLen; 
        } else { 
            for (int j=read-1;j>=1; --j) ungetc(tmp[j], fin); 
        }

        if (symbLen == 1){ 
            uLen = big5_len(b0); 
            if (uLen == 2){
                int d = fgetc(fin);
                if (d != EOF && is_big5_follow((unsigned char)d)){ 
                    tmp[1] = (unsigned char)d;
                    symbLen = 2;   
                } else {
                    if (d != EOF) ungetc(d, fin); 
                }
            }
        }

        // handle non ASCII, UTF-8, Big-5 symbols (128~255)
        if (symbLen == 1){ 
            // count++ -> write_code
            write_code(symb[b0].code, strlen(symb[b0].code), fout); 
            continue;
        }

        // 3. write multibyte symbol code
        for(int i=BYTE_MAX;i<used;i++){ 
            if(symb[i].useLen==symbLen && memcmp(symb[i].chr,tmp,symbLen)==0){ 
                // count++ -> write_code
                write_code(symb[i].code, strlen(symb[i].code), fout); 
                break; 
            }
        }
    }
    // ---------------- end of input file -----------------------
    write_code(symb[used-1].code, strlen(symb[used-1].code), fout); // write EOF code
    if (bitInBuff> 0) {
        // add 0 to fill last byte
        byteBuffer <<= (8 - bitInBuff);
        fputc(byteBuffer, fout); // last byte
    }
    fclose(fin);
    fclose(fout);
    return 0;
}