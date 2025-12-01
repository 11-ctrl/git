#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// max symbol length (UTF-8 or Big5)
#define MAX_SYMB_LEN 4

// huffman tree node structure
typedef struct Node {
    struct Node *left;    // left child (bit 0)
    struct Node *right;   // right child (bit 1)
    int is_leaf;          // is leaf node
    
    unsigned char chr[MAX_SYMB_LEN]; // symbol bytes
    int useLen;           // symbol byte length
} Node;

// huffman tree root
Node *Root = NULL;

// ------------------ create a new node -------------------------
Node* create_node() {
    Node *node = (Node*)calloc(1, sizeof(Node));
    return node;
}

// ------------------- insert code to huffman tree ------------------------
//  root, code, chr(symbol), len(symbol length)
void insert_code(Node *root, const char *code, const unsigned char *chr, int len) {
    Node *curr = root;
    const char *p = code;
    
    // follow the code path 0 or 1 until the end, then build the tree
    while (*p != '\0') {
        if (*p == '0') {
            if (curr->left == NULL) {
                curr->left = create_node();
            }
            curr = curr->left;
        } else if (*p == '1') {
            if (curr->right == NULL) {
                curr->right = create_node();
            }
            curr = curr->right;
        }
        p++;
    }
    
    // go to leaf node, set symbol
    curr->is_leaf = 1;
    memcpy(curr->chr, chr, len);
    curr->useLen = len;
}

// --------------------- build tree with codebook ------------------------
// search codebook line backwards to parse fields
// CSV : "Symbol",count,prob,code,info
void parse_and_build(char *line) {
    // remove newline characters
    int len = strlen(line);
    while(len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
    if (len == 0) return;

    // find code and symbol fields by scanning backwards
    // find last 4 commas, second last is code, fourth last is symbol
    char *ptr = line + len - 1;
    int comma_cnt = 0;
    char *code_start = NULL;
    char *symbol_end = NULL; // address of the end of symbol field

    while (ptr >= line) {
        if (*ptr == ',') {
            comma_cnt++;
            if (comma_cnt == 2) {
                // code, at the second last comma
                code_start = ptr + 1;
                // 將這個逗號改成 \0，切斷 code 與後面的 info
                // 注意：原本格式是 code,info。我們其實只需要 code 開頭
                // 但為了取字串，我們在 code 的下一個逗號(倒數第1個)也切斷比較保險
                // 這裡簡化處理：直接抓 code_start
            } 
            else if (comma_cnt == 4) {
                // 找到 Symbol 結束的位置 (在倒數第4個逗號之前)
                symbol_end = ptr;
                *symbol_end = '\0'; // 切斷！這裡之前就是 Symbol 字串
                break; 
            }
        }
        ptr--;
    }

    // 簡單的錯誤檢查
    if (comma_cnt < 4 || code_start == NULL) return;

    // 處理 Code 欄位：可能後面還有逗號連著 info，需要切斷
    // code_start 目前指向 "010101,12.34"，我們要切掉逗號
    char *p = code_start;
    while(*p){
        if(*p == ',') { *p = '\0'; break; }
        p++;
    }

    // 3. 處理 Symbol 欄位 (line 開頭到 symbol_end)
    char *raw_sym = line;
    
    // 去除前後的引號 " "
    if (raw_sym[0] == '"') raw_sym++; // 跳過第一個引號
    int rlen = strlen(raw_sym);
    if (rlen > 0 && raw_sym[rlen-1] == '"') raw_sym[rlen-1] = '\0'; // 去掉最後一個引號

    // 4. 還原特殊字元 (與 Encoder 對應)
    unsigned char symbol[4] = {0};
    int symLen = 0;

    if (strcmp(raw_sym, "EOF") == 0) {
        // 特殊標記 EOF
        strcpy((char*)symbol, "EOF");
        symLen = 3; 
    } else if (strcmp(raw_sym, "\\n") == 0) {
        symbol[0] = '\n'; symLen = 1;
    } else if (strcmp(raw_sym, "\\r") == 0) {
        symbol[0] = '\r'; symLen = 1;
    } else if (strcmp(raw_sym, "\\t") == 0) {
        symbol[0] = '\t'; symLen = 1;
    } else if (strcmp(raw_sym, "\\\"") == 0) { // \" -> "
        symbol[0] = '\"'; symLen = 1;
    } else if (strcmp(raw_sym, "\\\\") == 0) { // \\ -> \
        symbol[0] = '\\'; symLen = 1;
    } else {
        // 一般字元，處理雙引號還原 (Unescape)
        int i = 0, j = 0;
        while (raw_sym[i] != '\0') {
            // 如果遇到 "" 就變成 "
            if (raw_sym[i] == '"' && raw_sym[i+1] == '"') {
                symbol[j++] = '"';
                i += 2;
            } else {
                symbol[j++] = raw_sym[i++];
            }
        }
        symbol[j] = '\0'; // 補上結尾符號
        symLen = j;       // 更新正確長度
    }

    // 5. 插入樹中
    insert_code(Root, code_start, symbol, symLen);
}


// ---------------------- main ---------------------------
int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s output_file codebook_csv encoded_bin\n", argv[0]);
        return -1;
    }

    FILE *fout = fopen(argv[1], "wb");
    FILE *fcsv = fopen(argv[2], "r");
    FILE *fin  = fopen(argv[3], "rb");

    if (!fout || !fcsv || !fin) {
        perror("File open error");
        return -1;
    }

    // intialize Huffman Tree
    Root = create_node();

    // read codebook and build Huffman Tree
    char lineBuf[1024];
    while (fgets(lineBuf, sizeof(lineBuf), fcsv)) {
        parse_and_build(lineBuf);
    }
    printf("Huffman Tree built successfully.\n");
    fclose(fcsv);

    // decode the file
    Node *curr = Root;
    int c;
    long total_bytes = 0;
    int eof_found = 0;

    // 讀取每一個 byte
    while ((c = fgetc(fin)) != EOF) {
        if (eof_found) break; 

        // 讀取 byte 中的每一個 bit (7 -> 0)
        for (int i = 7; i >= 0; i--) {
            int bit = (c >> i) & 1;

            if (bit == 0) {
                curr = curr->left;
            } else {
                curr = curr->right;
            }

            // 錯誤檢查：如果路徑不存在 (樹建錯了或檔案壞了)
            if (curr == NULL) {
                fprintf(stderr, "Error: Invalid path (code not found in tree).\n");
                goto CLEANUP;
            }

            // 到達葉子節點
            if (curr->is_leaf) {
                // 檢查是否為 EOF
                if (curr->useLen == 3 && strncmp((char*)curr->chr, "EOF", 3) == 0) {
                    eof_found = 1;
                    break; // 跳出 bit 迴圈
                }

                // 寫入解碼後的字元
                fwrite(curr->chr, 1, curr->useLen, fout);
                total_bytes++;

                // 重置回樹根，準備解下一個字
                curr = Root;
            }
        }
    }

CLEANUP:
    printf("Decoding finished. Total symbols: %ld\n", total_bytes);
    fclose(fin);
    fclose(fout);
    
    return 0;
} 