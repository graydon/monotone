int sqlite3KeywordCode(const char *z, int n){
  static const char zText[500] =
    "ABORTABLEFTEMPORARYAFTERAISELECTHENDATABASEACHECKEYALTEREFERENCES"
    "CAPELSEXCEPTRANSACTIONATURALIKEXCLUSIVEXPLAINITIALLYANDEFAULT"
    "RIGGEREINDEXATTACHAVINGLOBEFOREIGNORENAMEAUTOINCREMENTBEGINNER"
    "EPLACEBETWEENOTNULLIMITBYCASCADEFERRABLECASECOLLATECOMMITCONFLICT"
    "CONSTRAINTERSECTCREATECROSSTATEMENTCURRENT_DATECURRENT_TIMESTAMP"
    "RAGMATCHDEFERREDELETEDESCDETACHDISTINCTDROPRIMARYFAILFROMFULL"
    "GROUPDATEIMMEDIATEINSERTINSTEADINTOFFSETISNULLJOINORDERESTRICT"
    "OUTERIGHTROLLBACKROWHENUNIONUNIQUEUSINGVACUUMVALUESVIEWHERE";
  static const unsigned char aHash[154] = {
       0,  18,  95,   0,   0, 100,  99,   0,  66,   0,   0,   0,   0,
      33,   0,  56,   0, 105,  30,   0,   0,   0,   0,   0,   0,   0,
       0, 106,   5,  38,   0,  74,  58,  35,  64,  59,   0,   0,  72,
      73,  68,  12,  29,  57,  19,   0,   0,  26,  75,   0,   0,  15,
       0,   0,   0,  46,   0,  49,   0,   0,   0,   0,  87,   0,  41,
      36,   0,  85,  82,   0,  78,  81,  27,   0,   0,  65,  43,  40,
      69,  60,   0,  61,   0,  62,   0,  92,  83,  70,   0,  21,   0,
       0,  88,  89,  93,   0,   0,   0,   0,   0,   0,   0,  77,   0,
       0,   0,   0,   0,  52,  86,  48,  51,  63,   0,   0,   0,   0,
      23,   2,   0,  34,   0,   3,  53, 102,   0,   0,  28,   0, 103,
       0,  50,  96, 107,   0,   0,   0,   0,   0,  90,   0,   0,   0,
       0,  10,  44,   0,   0,   0,   0, 101,  22,   0, 104,
  };
  static const unsigned char aNext[107] = {
       0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   7,
       0,  14,   0,  13,   0,   0,   0,   0,   0,   0,   0,   0,   0,
       0,   0,  20,   0,   0,   9,   0,   0,   0,   0,   0,   0,  31,
      25,   0,   0,   0,   0,   0,   0,   0,   0,   0,  16,  24,   8,
       0,  39,   0,   0,  37,   0,  55,   0,   0,   0,   0,   0,   0,
       0,   0,  67,   0,  45,   0,  11,   0,   0,   0,   0,  47,   0,
       0,   1,   0,  80,  76,   0,   0,  42,  17,  71,   0,   0,   0,
      54,   0,   0,  91,  94,   0,   6,  79,  32,   4,  84,  98,  97,
       0,   0,   0,
  };
  static const unsigned char aLen[107] = {
       5,   5,   4,   4,   9,   2,   5,   5,   6,   4,   3,   8,   2,
       4,   5,   3,   5,  10,   6,   4,   6,  11,   2,   7,   4,   9,
       7,   9,   3,   3,   7,   7,   7,   5,   6,   6,   4,   6,   3,
       7,   6,   6,  13,   2,   2,   5,   5,   7,   7,   3,   7,   4,
       5,   2,   7,   3,  10,   4,   7,   6,   8,  10,   9,   6,   5,
       9,  12,  12,  17,   6,   5,   8,   6,   4,   6,   8,   2,   4,
       7,   4,   4,   4,   5,   6,   9,   6,   7,   4,   2,   6,   3,
       6,   4,   5,   8,   5,   5,   8,   3,   4,   5,   6,   5,   6,
       6,   4,   5,
  };
  static const unsigned short int aOffset[107] = {
       0,   4,   7,  10,  10,  14,  19,  23,  26,  31,  33,  35,  40,
      42,  44,  48,  51,  55,  63,  68,  71,  76,  85,  86,  92,  95,
     103, 108, 113, 117, 119, 125, 131, 133, 138, 143, 148, 151, 153,
     153, 157, 161, 167, 169, 171, 180, 183, 187, 194, 200, 200, 203,
     206, 211, 213, 214, 218, 228, 232, 239, 245, 253, 260, 269, 275,
     279, 288, 300, 300, 316, 320, 325, 332, 338, 342, 348, 349, 356,
     359, 366, 370, 374, 378, 381, 387, 396, 402, 409, 412, 412, 415,
     418, 424, 428, 432, 440, 444, 449, 457, 459, 463, 468, 474, 479,
     485, 491, 494,
  };
  static const unsigned char aCode[107] = {
    TK_ABORT,      TK_TABLE,      TK_JOIN_KW,    TK_TEMP,       TK_TEMP,       
    TK_OR,         TK_AFTER,      TK_RAISE,      TK_SELECT,     TK_THEN,       
    TK_END,        TK_DATABASE,   TK_AS,         TK_EACH,       TK_CHECK,      
    TK_KEY,        TK_ALTER,      TK_REFERENCES, TK_ESCAPE,     TK_ELSE,       
    TK_EXCEPT,     TK_TRANSACTION,TK_ON,         TK_JOIN_KW,    TK_LIKE,       
    TK_EXCLUSIVE,  TK_EXPLAIN,    TK_INITIALLY,  TK_ALL,        TK_AND,        
    TK_DEFAULT,    TK_TRIGGER,    TK_REINDEX,    TK_INDEX,      TK_ATTACH,     
    TK_HAVING,     TK_GLOB,       TK_BEFORE,     TK_FOR,        TK_FOREIGN,    
    TK_IGNORE,     TK_RENAME,     TK_AUTOINCR,   TK_TO,         TK_IN,         
    TK_BEGIN,      TK_JOIN_KW,    TK_REPLACE,    TK_BETWEEN,    TK_NOT,        
    TK_NOTNULL,    TK_NULL,       TK_LIMIT,      TK_BY,         TK_CASCADE,    
    TK_ASC,        TK_DEFERRABLE, TK_CASE,       TK_COLLATE,    TK_COMMIT,     
    TK_CONFLICT,   TK_CONSTRAINT, TK_INTERSECT,  TK_CREATE,     TK_JOIN_KW,    
    TK_STATEMENT,  TK_CDATE,      TK_CTIME,      TK_CTIMESTAMP, TK_PRAGMA,     
    TK_MATCH,      TK_DEFERRED,   TK_DELETE,     TK_DESC,       TK_DETACH,     
    TK_DISTINCT,   TK_IS,         TK_DROP,       TK_PRIMARY,    TK_FAIL,       
    TK_FROM,       TK_JOIN_KW,    TK_GROUP,      TK_UPDATE,     TK_IMMEDIATE,  
    TK_INSERT,     TK_INSTEAD,    TK_INTO,       TK_OF,         TK_OFFSET,     
    TK_SET,        TK_ISNULL,     TK_JOIN,       TK_ORDER,      TK_RESTRICT,   
    TK_JOIN_KW,    TK_JOIN_KW,    TK_ROLLBACK,   TK_ROW,        TK_WHEN,       
    TK_UNION,      TK_UNIQUE,     TK_USING,      TK_VACUUM,     TK_VALUES,     
    TK_VIEW,       TK_WHERE,      
  };
  int h, i;
  if( n<2 ) return TK_ID;
  h = (sqlite3UpperToLower[((unsigned char*)z)[0]]*5 + 
      sqlite3UpperToLower[((unsigned char*)z)[n-1]]*3 +
      n) % 154;
  for(i=((int)aHash[h])-1; i>=0; i=((int)aNext[i])-1){
    if( aLen[i]==n && sqlite3StrNICmp(&zText[aOffset[i]],z,n)==0 ){
      return aCode[i];
    }
  }
  return TK_ID;
}
