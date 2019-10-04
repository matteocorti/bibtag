/* Program to add tags to Bibtex database entries  */
/* Author: Ronald L. Rivest                        */
/* Date: May 29, 1995                              */

#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <malloc.h>

#define TRUE  1
#define FALSE 0
#define STRINGSIZE 1000          /* max length of author name or ... */
#define BIGSTRINGSIZE 10000      /* for text before entries or attr/values */
#define MAXATTRIBUTES 100        /* max number of attributes in an entry */
#define MAXAUTHORS 40            /* max number authors on a paper */
#define MAXSTRINGS 1000          /* max number of string definitions */
#define MAXENTRIES 10000         /* max number of entries in a database file */

/* *** INPUT/OUTPUT DEFINITIONS *** */
int  InputChar;                  /* input char or EOF for end of file seen */
int  InputCol = 0;               /* column that input char was read from */
char InputFileName[STRINGSIZE];  /* File name for Bibtex database file */
FILE *InputFile;                 /* Bibtex database file */
char OutputFileName[STRINGSIZE]; /* File name for output Bibtex database file*/
FILE *OutputFile;                /* Bibtex output database file */
FILE *MessageFile;               /* Error and diagnostic messages */
int  EOFSeen;                    /* True if EOF seen on this file */

/* *** REPRESENTATION OF AN ENTRY *** */
struct Entry 
{
  char *InitialComments;         /* Text occuring before the entry type */
  char *EntryType;               /* type of Bibtex entry e.g. "@article" */
  char *StringDef;               /* definition, if a string constant */
  char *EntryTag;                /* Bibtex Tag e.g. "Rivest92" */
  char *NewEntryTag;             /* New bibtex entry tag */
  int  EntrySize;                /* Number of attribute/value pairs */
  char *EntryAttribute[MAXATTRIBUTES]; 
                                 /* Bibtex attribute name eg "year" */
  char *EntryValue[MAXATTRIBUTES];
                                 /* Bibtex value for attribute eg "1992" */
  int  IsCrossRef;               /* True if this is cross-reference target */
  struct Entry *CrossRef;        /* points to cross-reference target, if any */
} ;
char   *InitialText;               /* First text in the file */
struct Entry *Preamble;            /* pointer to preable entry */
struct Entry *StringArray[MAXSTRINGS];  /* pointers to string entries */
int    NumberOfStrings;                 /* total number of strings defined */
struct Entry *EntryArray[MAXENTRIES];   /* pointers to regular entries */
int    NumberOfEntries;                 /* total number of entries defined */

/* *** VARIABLES USED IN RECOMPUTING TAG *** */
char NewEntryTag[STRINGSIZE];    /* Temporary variable for new entry tag */
int  UseHyphens = TRUE;          /* Use hyphens in tag ? */            
int  CommaSeen;                  /* Comma seen in this name */
int  CommaJustSeen;              /* Comma just seen after this token */
int  FirstAuthorNameLengthBound = 12; 
                                 /* Max number of characters from (first)
			            author's name allowed in new tag */
int  SecondAuthorNameLengthBound = 2; /* Same for second and later authors */
int  AuthorBound = 6;            /* Bound on number of authors allowed */
char *NamePrefix[10] =           /* Prefixes that count as part of last name */
    {"De", "Di", "La", "El", "Von", "Van"};
int  NamePrefixCount = 6;        /* number of such prefixes */
int  FirstTitleWordLengthBound = 1; 
                                 /* Max number of characters from (first)
			            title word allowed in new tag */
int  SecondTitleWordLengthBound = 1;  /* Same for second and later title words */
int  TitleWordCountBound = 5;    /* Max number of title words allowed in tag */
char *CommonWords[30] =
    {"A","An","And","Are","But","By","For","From","In","Is",
       "Of","On","Over","The","To","Was","Were","With"};
int  NumberOfCommonWords = 18;
int  CheckDigitsWanted = 0;      /* Number of check digits wanted in tag */
int  YearDigitsWanted = 2;       /* Number of digits of year wanted in tag */
#define HASHTABLESIZE   19661    /* Size of hash table */

char HashTable[HASHTABLESIZE];   /* Hash table for detecting collisions */

/* *** VARIABLES CONTROLLING OUTPUT FORMAT *** */
int  SaveOldTags = FALSE;        /* If replacing tags, then save old tag
				    as attribute "oldtag" when changed */
int  SortSwitch = TRUE;          /* sort output into order by tag? */
int  AttributeIndent = 0;        /* Indentation level for attributes */
int  ValueIndent = 15;           /* Indentation level for values */
char LeftValueDelimiter = '{';   /* On output, what to start off value with */
char RightValueDelimiter = '}';  /* On output, what to end value with */
int  CompactEqualsSign = FALSE;  /* On output, use attr=value instead of
                                                   attr = value */

char *mymalloc(int n)
{
  char *p = (char *)malloc(n);
  if (p==NULL) 
    { 
      fprintf(MessageFile,"\nMemory allocation failure.\n");
      exit(0);
    }
  return(p);
}

/* GetC: Get a character from the input file 
 * Returns EOF and sets EOFSeen to TRUE if no more input.
 * Also keeps track of column input was read from in InputCol, so that
 * we can tell if an '@' came from column 1 or not.
 */
int GetC()                       
{
   if (EOFSeen) return(0);
   InputChar = getc(InputFile);
   if (InputChar == EOF) EOFSeen = TRUE;
   if (InputChar == '\r' || InputChar == '\n') InputCol = 0;
   else InputCol++;
   return(InputChar);
}

/* SkipSpace: Skip any white space characters from input. Stop if EOF.
 */ 
void SkipSpace()
{
   while (!EOFSeen && isspace(InputChar)) 
     GetC();
}

/* SkipChar(c): skip character c if it is next input character.
 *              (But first skip any whitespace characters.)
 *              If the next non-space character is not c, do nothing.
 */
void SkipChar(char c)
{                       
  SkipSpace();
  if (InputChar == c) 
    GetC();
}

/* SkipToAtSign(): skip input characters until we get to an '@' in col 1.
 *                 Return string of all characters skipped over.
 */
char *SkipToAtSign()
{ char buffer[BIGSTRINGSIZE];
  char *p = buffer;
  int  n = BIGSTRINGSIZE;
  while (!EOFSeen && (InputChar != '@' || InputCol != 1))
    {
      if (n>1)
	{
	  *p++ = InputChar;
	  n--;
	}
      else
	{
	  fprintf(MessageFile,"\nBibtag: text before an entry is too large!");
	  fprintf(MessageFile,"\nBibtag: test is lost!");
	  p = buffer;
	  n = BIGSTRINGSIZE;
	}
      GetC();
    }
  *p = 0;
  return(strdup(buffer));
}

/* GetToken(char c): 
 *             Read characters from input stream.
 *             Skip initial whitespace.
 *             Put token into new string.
 *             Used for obtaining: entry type, entry tag, attribute name, 
 *             and value.
 *             Skips character 'c' if it follows the token. (If c==0 then
 *                no skipping is done.)
 */
char *GetToken(char c)
{ 
  int i = 0;
  int n = BIGSTRINGSIZE;
  char buffer[BIGSTRINGSIZE];
 top:
  SkipSpace();
  if (InputChar == EOF) return(NULL);
  if (InputChar == '"')
    { /* collecting a string constant */
      /* string returned is enclosed in double quotes or delimiters */
      buffer[i++] = InputChar;
      GetC();
      while (i<n-1 && !EOFSeen && InputChar != '"')
	{ buffer[i++] = InputChar; 
	  if (InputChar=='\\')
	    { GetC();
	      buffer[i++] = InputChar;
	      GetC();
	    }
	  else
	      GetC();
        }
      if (InputChar != '"')
	{
	  fprintf(MessageFile,"\nBibtag: GetToken error (too long)!");
	  fprintf(MessageFile,"\nBibtag: Skipping text: %s",buffer);
	  /* fprintf(MessageFile,"\nBibtag: Skipping text: %s",
	     SkipToAtSign()); */
	  return(NULL);
	}
      SkipChar('"');
      buffer[i++] = '"';
      buffer[i] = 0;
    }
  else if (InputChar == '{')
    { /* token is brace-enclosed string */
      /* count nesting of braces to make this work correctly */
      /* string returned includes outer level of braces found */
      int bracelevel = 1;
      buffer[i++] = InputChar;
      GetC();
      while (i<n-1 && !EOFSeen && 
	     (InputChar != '}' || bracelevel > 1))
	{ buffer[i++] = InputChar; 
          if (InputChar=='{') bracelevel++;
          if (InputChar=='}') bracelevel--;
          GetC();
        }
      if (InputChar != '}')
	{
	  fprintf(MessageFile,"\nBibtag: GetToken error (too long)!");
	  fprintf(MessageFile,"\nBibtag: Skipping text: %s",buffer);
	  /* fprintf(MessageFile,"\nBibtag: Skipping text: %s",
	     SkipToAtSign()); */
	  return(NULL);
	}
      SkipChar('}');
      buffer[i++] = '}';
      buffer[i] = 0;
    }
  else if (InputChar == '@' || isalnum(InputChar))
    { /* token is alphanumeric or begins with an at sign */
      while (i<n-1 && !EOFSeen && 
	     (InputChar != ',' && InputChar != '}' && 
	      InputChar != '{' && InputChar != '=' && !isspace(InputChar)))
	{ buffer[i++] = InputChar; 
          GetC();
        };
      buffer[i] = 0;
    }
  else if (InputChar == '#')
    { /* concatenation character */
      buffer[i++] = ' ';
      buffer[i++] = InputChar;
      buffer[i++] = ' ';
      buffer[i] = 0;
      GetC();
    }
  else 
    { /* Problem !! */
      fprintf(MessageFile,"\nBibtag: GetToken error!");
      fprintf(MessageFile,"\nBibtag: Skipping text: %s",buffer);
      /* fprintf(MessageFile,"\nBibtag: Skipping text: %s",SkipToAtSign()); */
      return(NULL);
    }
  /* The following line is a hack to handle the case that we are looking
     for a ',' to end the token, but the comma is missing because it is
     the last of the attribute/value pairs
     */
  if (c!=0) SkipSpace();
  if (c!=0 && InputChar != c && InputChar != '}') goto top;
  if (c!=0) SkipChar(c);
  return(strdup(buffer));
}

/* PrintEntry: Output entry.
 *             Change tag if requested.
 *             Output "oldtag" attribute/value pair if newtag is different.
 *             Honor indentation requests.
 */
void PrintEntry(struct Entry *e)
{ int i,j,k;
  fprintf(OutputFile,"%s",e->InitialComments);
  fprintf(OutputFile,"%s",e->EntryType);
  if (strcasecmp(e->EntryType,"@string")==0)
      { /* entry type is string */
	fprintf(OutputFile,"%s",e->StringDef);
      }
  else
    { /* entry type is not string */
      fprintf(OutputFile,"{%s,",e->EntryTag);
      /* Now print out each attribute/value pair */
      for (i=0;i<e->EntrySize;i++)
	if (e->EntryAttribute[i]!=NULL)
	  { int c, lastc, col;
	    /* Indent for attribute */
	    fprintf(OutputFile,"\n");
	    for (j=0;j<AttributeIndent;j++) 
	      fprintf(OutputFile," ");
	    /* Print out attribute, after converting to lower case */
	    for (j=0;e->EntryAttribute[i][j];j++)
	      e->EntryAttribute[i][j] = tolower(e->EntryAttribute[i][j]);
	    fprintf(OutputFile,"%s",e->EntryAttribute[i]);
	    if (CompactEqualsSign) fprintf(OutputFile,"=");
	    else                   fprintf(OutputFile," = ");
	    /* Indent for Value */
	    for (j=strlen(e->EntryAttribute[i])+AttributeIndent+3;
		 j<ValueIndent;
		 j++)
	      fprintf(OutputFile," ");
	    /* Print out value */
	    lastc = ' ';
	    col = ValueIndent;
	    for (j=0;e->EntryValue[i][j];j++)
	      { c = e->EntryValue[i][j];
		if (isspace(c)) c = ' ';
		if (col>65 && c==' ')
		  { /* good place for a line break and re-indenting */
		    fprintf(OutputFile,"\n");
		    for (k=0;k<ValueIndent;k++) fprintf(OutputFile," ");
		    col = ValueIndent;
		    lastc = ' ';
		  }
		if (lastc!=' ' || c!=' ')
		  { /* print out this character */
		    fprintf(OutputFile,"%c",c);
		    col++;
		  }
		lastc = c;
	      }
	    if (i<e->EntrySize-1) fprintf(OutputFile,",");
	  }
      fprintf(OutputFile,"\n}");
    }
}

struct Entry *NewEntry()
{ int i;
  struct Entry *e = (struct Entry *)mymalloc(sizeof(struct Entry));
  e->InitialComments = NULL;
  e->EntryType = NULL;
  e->StringDef = NULL;
  e->EntryTag = NULL;
  e->NewEntryTag = NULL;
  e->EntrySize = 0;
  for (i=0;i<MAXATTRIBUTES;i++)
    {
      e->EntryAttribute[i] = NULL;
      e->EntryValue[i] = NULL;
    }
  e->IsCrossRef = FALSE;
  e->CrossRef = NULL;
  return(e);
}

char *GetValue(struct Entry *e, char *attribute)
{ struct Entry *ex;
  int i;
  for (i=1;i<e->EntrySize;i++)
    if (strcasecmp(attribute,e->EntryAttribute[i])==0)
      return(e->EntryValue[i]);
  /* Attribute not found; look for it in cross-ref */
  if (e->CrossRef == NULL) return(NULL);
  ex = e->CrossRef;
  for (i=1;i<ex->EntrySize;i++)
    if (strcasecmp(attribute,ex->EntryAttribute[i])==0)
      return(ex->EntryValue[i]);
  return(NULL);
}

/* GetEntry: Read an entire bibtex reference from the input stream.
 *           Skip over white space and other text first, printing it out.
 *           results --> EntryType, EntryTag, EntryAttribute, EntryValue
 *                       (or StringDef if it defines a string).
 */
struct Entry *GetEntry()
{
  struct Entry *e = NewEntry();
 GoToAtSign:
  e->EntrySize = 1; /* accounts for oldtag, if necessary to output */
  e->InitialComments = SkipToAtSign();
  /* Append CRs if necessary to ensure that @ will end up in first column */
  if (e->InitialComments == NULL)
      e->InitialComments = strdup("\n\n");
  else if (strlen(e->InitialComments) == 0)
    {
      free(e->InitialComments);
      e->InitialComments = strdup("\n\n");
    }
  else if (e->InitialComments[strlen(e->InitialComments)-1] != '\n')
    {
      char *com = mymalloc(strlen(e->InitialComments)+2);
      strcpy(com,e->InitialComments);
      free(e->InitialComments);
      strcat(com,"\n");
    }
  if (EOFSeen) return(e);
  e->EntryType = GetToken(0);
  if (strcasecmp(e->EntryType,"@string")==0)
    { /* entry type is String */
      e->StringDef = GetToken(0);
    }
  else
    { /* entry type is not String */
      SkipChar('{'); 
      e->EntryTag = GetToken(',');
      while (InputChar != '}')
	{ 
	  e->EntryAttribute[e->EntrySize] = GetToken('=');
	  e->EntryValue[e->EntrySize] = GetToken(',');
	  if (++(e->EntrySize) >= MAXATTRIBUTES-1) 
	    { 
	      fprintf(MessageFile,"Bibtag: %s has too many attributes!\n",
		      e->EntryTag); 
	      fprintf(MessageFile,"Bibtag: Text lost!!!\n");
	      fprintf(MessageFile,"Bibtag: Skipping to next at-sign!!!\n");
	      goto GoToAtSign;
	    }
	  SkipSpace();
	}
      SkipChar('}');
      /* SkipSpace(); */
    }
  if (GetValue(e,"crossrefonly")!=NULL) 
    e->IsCrossRef = TRUE;
  return(e);
}

/* ScanToken(p,ans): Scan string starting at p for next token.
 *                   Result goes into string ans.
 *                   Similar to p = strtok(p," }\"\t\n\r~")
 *                     in that repeated calls get next token, etc.
 *                   Used to parse author name list into tokens.
 *                   Side effect of setting CommaJustSeen TRUE if token
 *                     returned was followed by a comma.
 */
char *ScanTokenPtr = NULL;
char ScanToken(char *p, char *ans)
{ 
  char *q = ans;
  int bracelevel = 0;
  if (p == NULL) p = ScanTokenPtr;
  CommaJustSeen = FALSE;
  while (*p)
    { 
      if (isalnum(*p)
	  || (UseHyphens && *p=='-') /* keep alphanums and optional hyphens */
	  || (*p=='\''))             /* keep single quotes */
	*ans++ = *p++;
      else if (*p=='{')          /* left brace: bump bracelevel */
	{ bracelevel++;
	  p++;
	}
      else if (*p=='}')          /* right brace: decrement bracelevel */
	{ bracelevel--;
	  p++;
	}
      else if (*p=='\\')         /* backslash: skip this quoted char */
        { p++;
	  if (*p) p++;
	}
      else if ((ispunct(*p) || isspace(*p)) && bracelevel == 0)
	                         /* stop scanning if punctuation or space
                                    outside of any braces */
	break;
      else                       /* otherwise copy character to output */
	*ans = *p++;
    }
  *ans = 0;                      /* terminate output string */
  while (*p!='{' && *p!='\\' && *p != '}' &&
	 (ispunct(*p) || isspace(*p))) 
                                 /* skip following spaces or punctuation
                                    except for braces and quoted chars */
    { if (*p == ',') CommaJustSeen = TRUE;
      p++;
    }
  ScanTokenPtr = p;              /* save pointer so we can continue scan */
  *q = toupper(*q);              /* force first char of output upper case */
}

AppendTitleToNewEntryTag(struct Entry *e)
{ int  i,j,k;
  int  TitleWordCount;
  char TitleWord[100][STRINGSIZE];
  char TitleToken[STRINGSIZE];
  char Title[STRINGSIZE];
  char *p;
  char *title;
  /* Find title */
  title = GetValue(e,"title");
  if (title == NULL)
    { /* No title */
      fprintf(MessageFile,"Bibtag: %s has no title!\n",e->EntryTag);
      return;
    }
  strcpy(Title,title);
  p = Title+1;
  TitleWordCount = 0;
  ScanToken(p,TitleToken);
  while (TitleToken[0]!=0)
    { 
      strcpy(TitleWord[TitleWordCount],TitleToken);
      TitleWordCount++;
      /* determine if this was a common word */
      for (i=0;
	   i<NumberOfCommonWords 
	   && strcasecmp(CommonWords[i],TitleToken)!=0;
	   i++) ;
      if (i!=NumberOfCommonWords)
	TitleWordCount--;
      ScanToken(NULL,TitleToken);
    }
  if (e->NewEntryTag != NULL)
    {
      strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  p = NewEntryTag + strlen(NewEntryTag);
  /* Process first title word */
  for (j=0,k=0;TitleWord[0][j]!=0&&0<TitleWordCountBound;j++)
    if (k<FirstTitleWordLengthBound
	&& (isalpha(TitleWord[0][j])||
	    (UseHyphens && TitleWord[0][j]=='-')))
      { k++;
	*p++ = TitleWord[0][j];
      }
  /* Process remaining title words */
  for (i=1;i<TitleWordCount&&i<TitleWordCountBound;i++)
    for (j=0,k=0;TitleWord[i][j]!=0;j++)
      if (k<SecondTitleWordLengthBound
	  && (isalpha(TitleWord[i][j])||
	      (UseHyphens && TitleWord[i][j]=='-')))
	{ k++;
	  *p++ = TitleWord[i][j];
	}
  *p = 0;
  e->NewEntryTag = strdup(NewEntryTag);
}

AppendYearToNewEntryTag(struct Entry *e)
{ int i,j,k;
  char *p,*yr;
  if (e->NewEntryTag != NULL)
    { strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  p = NewEntryTag + strlen(NewEntryTag);
  /* Get digits of year */
  yr = GetValue(e,"year");
  if (yr != NULL)
      {
	for (j=0,k=0;yr[j]!=0;j++)
	  if (isdigit(yr[j])||yr[j]=='?')
	    { k++;
	      if (k>4-YearDigitsWanted) *p++ = yr[j];
	    }
      }
  else
    { /* Year digits will be ?'s */
      fprintf(MessageFile,"Bibtag: %s has no year!\n",e->EntryTag);
      for (i=0;i<YearDigitsWanted;i++) *p++ = '?';
    }
  *p = 0;
  e->NewEntryTag = strdup(NewEntryTag);
}

AppendAuthorInfoToNewEntryTag(struct Entry *e)
{ int i,j,k;
  char *p;
  char Authors[STRINGSIZE];
  char AuthorToken[STRINGSIZE];
  int  AuthorCount;                        /* Number of Authors for entry */
  char AuthorName[MAXAUTHORS][STRINGSIZE]; /* Last names of authors */
  int LastTokenWasNamePrefix;
  char *author;
  /* Get the author (or, failing that, the editor) field */
  author = GetValue(e,"author");
  if (author == NULL)
    { /* No authors; search for editors instead */
      author = GetValue(e,"editor");
      if (author == NULL)
	{ /* Suppress error message if this is a cross ref target */
	  if (e->IsCrossRef == FALSE)
	    { 
	      fprintf(MessageFile,"Bibtag: %s has no authors or editors!\n",
		      e->EntryTag);
	    }
	  return;
	}
    }
  strcpy(Authors,author);
  p = Authors+1;
  AuthorCount = 1;
  AuthorName[0][0] = 0;
  LastTokenWasNamePrefix = FALSE;
  CommaSeen = FALSE;
  ScanToken(p,AuthorToken);
  while (AuthorToken[0]!=0)
    { 
      if (strcasecmp("and",AuthorToken)==0) 
	{ /* "and" seen; get ready to scan new name */
	  AuthorCount++;
	  AuthorName[AuthorCount-1][0] = 0;
	  LastTokenWasNamePrefix = FALSE;
	  CommaSeen = FALSE;
	}
      else
	{ /* not an "and" */
	  if (CommaSeen==FALSE) /* doesn't count current scan token */
	    { if (LastTokenWasNamePrefix) /* keep it, and add on */
		strcat(AuthorName[AuthorCount-1],AuthorToken);
 	      else                        /* just keep new token */
		if (strcasecmp("jr",AuthorToken)!=0)  /* but no jr's allowed */
		  strcpy(AuthorName[AuthorCount-1],AuthorToken);
	    }
	  if (CommaJustSeen) CommaSeen = TRUE;
	  /* determine if this was a prefix */
	  for (i=0;
	       i<NamePrefixCount && strcasecmp(NamePrefix[i],AuthorToken)!=0;
	       i++) ;
	  if (i==NamePrefixCount)
	    LastTokenWasNamePrefix = FALSE;
	  else
	    LastTokenWasNamePrefix = TRUE;
	}
      ScanToken(NULL,AuthorToken);
    }
  if (e->NewEntryTag!=NULL)
    { strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  p = NewEntryTag + strlen(NewEntryTag);
  /* Process first author's name */
  for (j=0,k=0;AuthorName[0][j]!=0;j++)
    if (k<FirstAuthorNameLengthBound
	&& (isalpha(AuthorName[0][j])||
	    (UseHyphens && AuthorName[0][j]=='-')))
      { k++;
	*p++ = AuthorName[0][j];
      }
  /* Process remaining authors' names */
  for (i=1;i<AuthorCount;i++)
    if (i<AuthorBound)
      { for (j=0,k=0;AuthorName[i][j]!=0;j++)
	  if (k<SecondAuthorNameLengthBound
	      && (isalpha(AuthorName[i][j])||
		  (UseHyphens && AuthorName[i][j]=='-')))
	    { k++;
	      *p++ = AuthorName[i][j];
	    }
      }
  *p = 0;
  e->NewEntryTag = strdup(NewEntryTag);
}

AppendCheckDigitsToNewEntryTag(struct Entry *e)
{
  int i,j,check;
  int c;
  char *p;
  check = 0;
  for (i=1;i<e->EntrySize;i++)
    if (strcasecmp(e->EntryAttribute[i],"oldtag")!=0 &&
	strcasecmp(e->EntryAttribute[i],"newtag")!=0)
      { 
	for (j=0;e->EntryValue[i][j]!=0;j++)
	  { c = e->EntryValue[i][j];
	    if (isalpha(c)||isdigit(c))
	      check = (check * 23 + c) % 12345;
	  }
      }
  if (e->NewEntryTag!=NULL)
    { strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  p = NewEntryTag + strlen(NewEntryTag);
  for (i=0;i<CheckDigitsWanted;i++)
    { 
      if ((i&1)==0)
	{ *p++ = "bcdfghjklmnpqrstvwxyz"[check%21];
	  check = check / 21;
	}
      else
	{ *p++ = "aeiou"[check%5];
	  check = check / 5;
	}
    }
  *p = 0;
  e->NewEntryTag = strdup(NewEntryTag);
}

AppendOldExtensionsToNewEntryTag(struct Entry *e)
{
  if (e->NewEntryTag != NULL)
    { strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  if (strncmp(e->EntryTag,NewEntryTag,strlen(NewEntryTag))==0)
    strcpy(NewEntryTag,e->EntryTag);
  e->NewEntryTag = strdup(NewEntryTag);
}

int HashTableIndex(char *s)
{ /* compute hash table index for the given string.
     Don't worry about collisions */
  int i = 0;
  char *p = s;
  while (*p) 
    { i = (i*1231 + *p) % HASHTABLESIZE;
      p++;
    }
  return(i);
}

int GetHashEntry(char *s)
{
  return((int)HashTable[HashTableIndex(s)]);
}

void SetHashEntry(char *s)
{
  HashTable[HashTableIndex(s)] = 1;
}

AppendExtensionToMakeNewEntryTagUnique(struct Entry *e)
{ char ctr[10];
  int i;
  int taglen;
  if (e->NewEntryTag != NULL)
    { strcpy(NewEntryTag,e->NewEntryTag);
      free(e->NewEntryTag);
    }
  else
    NewEntryTag[0] = 0;
  if (GetHashEntry(NewEntryTag)==0) 
    { 
      SetHashEntry(NewEntryTag);
      e->NewEntryTag = strdup(NewEntryTag);
      return;
    }
  taglen = strlen(NewEntryTag);
  for (i=0;i<10;i++) 
    ctr[i] = 0;
  while (GetHashEntry(NewEntryTag))
    { NewEntryTag[taglen] = 0;
      i = 0;
      while (ctr[i]=='z') ctr[i++] = 'a';
      if (ctr[i]==0) ctr[i] = 'a';
      else           ctr[i]++;
      strcat(NewEntryTag,ctr);
    }
  e->NewEntryTag = strdup(NewEntryTag);
  SetHashEntry(NewEntryTag);
}

/* MakeDefaultNewEntryTag: Make default new entry tag for this entry.
   Do nothing if NewEntryTag already exists.
 */
void MakeDefaultNewEntryTag(e)
struct Entry *e;
{ 
  if (e->NewEntryTag == NULL)
    { /* no tag computed yet; presumably because no tag specifications */
      /* so compute default tag */
      AppendAuthorInfoToNewEntryTag(e);
      AppendYearToNewEntryTag(e);
      AppendOldExtensionsToNewEntryTag(e);
      AppendExtensionToMakeNewEntryTagUnique(e);
    }
}

void PrintUsage()
{
  fprintf(MessageFile,"Bibtag usage: bibtag inputfilename(s) options\n");
  fprintf(MessageFile,"Options: (their order matters!)\n");
  fprintf(MessageFile," -h        prints this usage table\n");
  fprintf(MessageFile," -in,m     sets attribute indentation to n and value indentation to m\n");
  fprintf(MessageFile," -b        surrounds values with braces\n");
  fprintf(MessageFile," -q        surrounds values with quotes\n");
  fprintf(MessageFile," -=        omits spaces around equals signs on output\n");
  fprintf(MessageFile," -oxx      (or -o xx) sets output file to xx\n");
  fprintf(MessageFile," --        forbids hyphens in tags\n");
  fprintf(MessageFile," -af,s,n   includes author's names, with at most f letters from the first,\n");
  fprintf(MessageFile,"           s from the second (and later), and at most n authors total\n");
  fprintf(MessageFile," -tf,s,n   includes title words, with at most f letters from the first,\n");
  fprintf(MessageFile,"           s from the second (and later), and at most n words total\n");
  fprintf(MessageFile," -yn       includes the last n digits of the year(default n = 2)\n");
  fprintf(MessageFile," -lxxx     includes literal string xxx in the tag\n");
  fprintf(MessageFile," -p        includes the previous tag in the new tag\n");
  fprintf(MessageFile," -cn       adds n check digits to the new tag (default n=1)\n");
  fprintf(MessageFile," -e        keeps existing extension if newly computed tag is prefix of old tag.\n");
  fprintf(MessageFile," -u        Adds characters to make new tags unique.\n");
  fprintf(MessageFile," -s        saves old tags in `oldtag' attribute\n");
  fprintf(MessageFile," -n        no sorting is done\n");
  fprintf(MessageFile,"A `newtag' attribute in an entry forces the tag to be the given value.");
  fprintf(MessageFile,"\n");
}

void ReadDataBase()
{
  struct Entry *e;
  EOFSeen = FALSE;
  InputCol = 0;
  GetC(); /* Prime the scanning routines by reading first character */
  SkipSpace();
  /* Input loop -- read the entire file */
  InitialText = SkipToAtSign();
  while (!EOFSeen)
    { 
      e = GetEntry();
      if (EOFSeen) break;
      if (strcasecmp("@preamble",e->EntryType)==0) 
	{ 
	  if (Preamble != NULL)
	    {
	      fprintf(MessageFile,"\nBibtag: More than one @preamble!");
	      fprintf(MessageFile,"\nBibtag: Earlier @preamble will be lost!");
	    }
	  Preamble = e;
	}
      else if (strcasecmp("@string",e->EntryType)==0) 
	{ if (NumberOfStrings==MAXSTRINGS)
	    { 
	      fprintf(MessageFile,"\nBibtag: Too many string definitions!\n");
	      exit(0);
	    }
	else
	  StringArray[NumberOfStrings++] = e;
	}
      else 
	{ if (NumberOfEntries == MAXENTRIES)
	    { 
	      fprintf(MessageFile,"\nBibtag: Too many entries in file!\n");
	      exit(0);
	    }
	  else
	    EntryArray[NumberOfEntries++] = e;
	}
    }
  fclose(InputFile);
}

void ResolveCrossReferences()
{ int i,j,k;
  struct Entry *e, *ex;
  for (i=0;i<NumberOfEntries;i++)
    { e = EntryArray[i];
      for (j=1;j<e->EntrySize;j++)
	if (strcasecmp("crossref",e->EntryAttribute[j])==0)
	  {
	    for (k=i+1;k<NumberOfEntries;k++)
	      { ex = EntryArray[k];
		if (strcasecmp(e->EntryValue[j],ex->EntryTag)==0 ||
		    strncasecmp(e->EntryValue[j]+1,
				ex->EntryTag,
				strlen(e->EntryValue[j])-2) == 0)
		  {
		    ex->IsCrossRef = TRUE;
		    e->CrossRef = ex;
		    break;
		  }
	      }
	    if (e->CrossRef==NULL)
	      fprintf(MessageFile,
		      "\nBibtag: Crossref %s for entry %s not found!",
		      e->EntryValue[j],
		      e->EntryTag);
	  }
    }
}

int EntryCompare(struct Entry *e1, struct Entry *e2)
{ 
  if (e1->IsCrossRef == 0 && e2->IsCrossRef == 1) return(-1);
  if (e1->IsCrossRef == 1 && e2->IsCrossRef == 0) return(1);
  return(strcmp(e1->EntryTag,e2->EntryTag));
}

int Partition(int p, int r)
{ struct Entry *x;
  int i = p-1;
  int j = r+1;
  int k = p + (random() % (r-p+1));
  x = EntryArray[k];
  EntryArray[k] = EntryArray[p];
  EntryArray[p] = x;
  while (TRUE)
    { struct Entry *e;
      do j--; while (EntryCompare(EntryArray[j],x)>0);
      do i++; while (EntryCompare(EntryArray[i],x)<0);
      if (i<j)
	{ e = EntryArray[i];
	  EntryArray[i] = EntryArray[j];
	  EntryArray[j] = e;
	}
      else 
	return(j);
    }
}

void QuickSortEntries(int p, int r)
{ int q;
  if (p<r)
    { q = Partition(p,r);
      QuickSortEntries(p,q);
      QuickSortEntries(q+1,r);
    }
}     

void SortEntries()
{ int p,r;
  if (SortSwitch)
    QuickSortEntries(0,NumberOfEntries-1);
}

/* Parse command line arguments */
void ParseAndExecuteCommandLine(argc,argv)
int argc;
char *argv[];
{ int i,j,k;
  char c1, c2;
  struct Entry *e;
  char *v;
  InitialText = strdup("");
  Preamble = NULL;
  NumberOfStrings = 0;
  NumberOfEntries = 0;
  if (argc==2 && argv[1][0]=='-' && tolower(argv[1][1]=='h'))
    { /* -h or -help option */
      PrintUsage(); 
      exit(0);
    }
  if (argc == 1)
    { /* no input file name given */
      InputFile = stdin;
      ReadDataBase();
      ResolveCrossReferences();
    }
  for (i=1;i<argc;i++)
    {
      if (argv[i][0]!='-')
	{ 
	  /* process an input file name */
	  strcpy(InputFileName,argv[i]);
	  if (InputFileName[0])
	    { 
	      InputFile = fopen(InputFileName,"r");
	      if (InputFile == NULL) 
		{ fprintf(MessageFile,
			  "\nBibtag: Input file open error: %s\n",
			  InputFileName); 
		  exit(0); 
		}
	    }
	  /* Input the entire database into memory */
	  ReadDataBase();
	  ResolveCrossReferences();
	}
      else
	{ /* process option */
	  c1 = tolower(argv[i][1]);
	  c2 = tolower(argv[i][2]);
	  if (c1=='o')
	    { /* Set output file name */
	      if (argv[i][2]!=0)
		strcpy(OutputFileName,argv[i]+2);
	      else
		{ 
		  i++;
		  if (i<argc)
		    strcpy(OutputFileName,argv[i]);
		  else
		    {
		      fprintf(MessageFile,
			      "\nBibtag: No file name given for -o option.");
		      exit(0);
		    }
		}
	      /* Now open the output file */
	      if (OutputFileName[0])
		{
		  OutputFile = fopen(OutputFileName,"w");
		  if (OutputFile == NULL) 
		    { fprintf(MessageFile,
			      "\nBibtag: Output file open error: %s",
			      OutputFileName); 
		      exit(0); 
		    }
		}
	    }
	  else if (c1=='n')
	    {
	      SortSwitch = FALSE;
	    }
	  else if (c1=='s')
	    {
	      SaveOldTags = TRUE;
	    }
	  else if (c1=='i')
	    { /* set indentation level for attributes and values */
	      AttributeIndent=atoi(argv[i]+2);
	      if (AttributeIndent<0) AttributeIndent = 0;
	      if (AttributeIndent>20) AttributeIndent= 20;
	      j = 0;
	      while (argv[i][j] && argv[i][j]!=',') j++;
	      if (argv[i][j]==',')
		{ /* set indentation level for values */
		  ValueIndent=atoi(argv[i]+j+1);
		  if (ValueIndent<0) ValueIndent = 0;
		  if (ValueIndent>40) ValueIndent= 40;
		}
	    }
	  else if (c1=='b' || c1 == 'q')
	    { /* Use braces for value delimiters on output */
	      int OldLeftValueDelimiter, OldRightValueDelimiter;
	      if (c1 == 'b')
		{ LeftValueDelimiter = '{';
		  RightValueDelimiter = '}';
		  OldLeftValueDelimiter = '"';
		  OldRightValueDelimiter = '"';
		}
	      else if (c1 == 'q')
		{ LeftValueDelimiter = '"';
		  RightValueDelimiter = '"';
		  OldLeftValueDelimiter = '{';
		  OldRightValueDelimiter = '}';
		}
	      for (k=0;k<NumberOfEntries;k++)
		{ e = EntryArray[k];
		  for (j=1;j<e->EntrySize;j++)
		    if (e->EntryValue[j][0] == OldLeftValueDelimiter &&
			e->EntryValue[j][strlen(e->EntryValue[j])-1] ==
			OldRightValueDelimiter)
		      { e->EntryValue[j][0] = LeftValueDelimiter;
			e->EntryValue[j][strlen(e->EntryValue[j])-1] =
			  RightValueDelimiter;
		      }
		}
	    }
	  else if (c1=='=')
	    { /* compact equals sign '=' instead of ' = ' on output */
	      CompactEqualsSign = TRUE;
	    }
	  else if (c1=='a') 
	    { /* author portion of tag field */
	      if (argv[i][2]!=0)
		FirstAuthorNameLengthBound = atoi(argv[i]+2);
	      j = 0;
	      while (argv[i][j]!=0&&argv[i][j]!=',') j++;
	      if (argv[i][j] == ',')
		{ j++;
		  SecondAuthorNameLengthBound = atoi(argv[i]+j);
		  while (argv[i][j]!=0&&argv[i][j]!=',') j++;
		  if (argv[i][j] == ',')
		    AuthorBound = atoi(argv[i]+j+1);
		}
	      for (k=0;k<NumberOfEntries;k++)
		AppendAuthorInfoToNewEntryTag(EntryArray[k]);
	    }
	  else if (c1=='t') 
	    { /* title portion of tag field */
	      if (argv[i][2]!=0)
		FirstTitleWordLengthBound = atoi(argv[i]+2);
	      j = 0;
	      while (argv[i][j]!=0&&argv[i][j]!=',') j++;
	      if (argv[i][j] == ',')
		{ j++;
		  SecondTitleWordLengthBound = atoi(argv[i]+j);
		  while (argv[i][j]!=0&&argv[i][j]!=',') j++;
		  if (argv[i][j] == ',')
		    TitleWordCountBound = atoi(argv[i]+j+1);
		}
	      for (k=0;k<NumberOfEntries;k++)
		AppendTitleToNewEntryTag(EntryArray[k]);
	    }
	  else if (c1=='c')
	    { /* check digits */
	      CheckDigitsWanted = 1; /* default */
	      if (c2!=0)
		CheckDigitsWanted = atoi(argv[i]+2);
	      for (k=0;k<NumberOfEntries;k++)
		AppendCheckDigitsToNewEntryTag(EntryArray[k]);
	    }
	  else if (c1=='-')
	    { /* Clear hyphen switch */
	      UseHyphens = FALSE;
	    }
	  else if (c1=='y')
	    { /* year */
	      if (c2!=0)
		YearDigitsWanted = atoi(argv[i]+2);
	      if (YearDigitsWanted<0) YearDigitsWanted = 0;
	      if (YearDigitsWanted>4) YearDigitsWanted = 4;
	      for (k=0;k<NumberOfEntries;k++)
		AppendYearToNewEntryTag(EntryArray[k]);
	    }
	  else if (c1=='e')
	    {
	      for (k=0;k<NumberOfEntries;k++)
		AppendOldExtensionsToNewEntryTag(EntryArray[k]);
	    }
	  else if (c1 == 'l')
	    {
	      for (k=0;k<NumberOfEntries;k++)
		{ struct Entry *e = EntryArray[k];
		  if (e->NewEntryTag != NULL)
		    { strcpy(NewEntryTag,e->NewEntryTag);
		      free(e->NewEntryTag);
		    }
		  else
		    NewEntryTag[0] = 0;
		  strcat(NewEntryTag,argv[i]+2);
		  e->NewEntryTag = strdup(NewEntryTag);
		}
	    }
	  else if (c1 == 'p')
	    {
	      for (k=0;k<NumberOfEntries;k++)
		{ struct Entry *e = EntryArray[k];
		  if (e->NewEntryTag != NULL)
		    { strcpy(NewEntryTag,e->NewEntryTag);
		      free(e->NewEntryTag);
		    }
		  else
		    NewEntryTag[0] = 0;
		  strcat(NewEntryTag,e->EntryTag);
		  e->NewEntryTag = strdup(NewEntryTag);
		}
	    }
	  else if (c1 == 'u')
	    {
	      for (k=0;k<HASHTABLESIZE;k++) 
		HashTable[k] = 0;
	      for (k=0;k<NumberOfEntries;k++)
		AppendExtensionToMakeNewEntryTagUnique(EntryArray[k]);
	    }
	  else
	    { /* Illegal option */
	      fprintf(MessageFile,"\nBibtag: Illegal option: %s",argv[i]);
	    }
        }
    }
}


void ReplaceTags()
{
  struct Entry *e;
  int k;
  char *v;
  { /* Replace tags in output file with newly computed ones */
    if (SaveOldTags)
      { /* save old tag in attribute "oldtag" 
	   when new tag is different 
	   */
	for (k=0;k<NumberOfEntries;k++)
	  { e = EntryArray[k];
	    MakeDefaultNewEntryTag(e);
	    if (strcmp(e->NewEntryTag,e->EntryTag)!=0)
	      { 
		if (e->EntryAttribute[0] != NULL) 
		  free(e->EntryAttribute[0]);
		e->EntryAttribute[0] = strdup("oldtag");
		if (e->EntryValue[0] != NULL) 
		  free(e->EntryValue[0]);
		e->EntryValue[0] = mymalloc(strlen(e->EntryTag)+3);
		e->EntryValue[0][0]=LeftValueDelimiter;
		e->EntryValue[0][1]=0;
		strcat(e->EntryValue[0],e->EntryTag);
		e->EntryValue[0][strlen(e->EntryTag)+1]=
		  RightValueDelimiter;
		e->EntryValue[0][strlen(e->EntryTag)+2]=0;
	      }
	  }
      }
    /* Now actually replace the old tag with the new one */
    /* Don't change tags of cross-ref targets */
    /* newtag overrides computed tag */
    for (k=0;k<NumberOfEntries;k++)
      { e = EntryArray[k];
	v = GetValue(e,"newtag");
	if (v != NULL)
	  {
	    if (e->NewEntryTag != NULL) 
	      free(e->NewEntryTag);
	    e->NewEntryTag = v;
	    if (v[0] == '"' || v[0] == '{' )
	      { /* strip off surrounding braces or quotes */
		strcpy(v,v+1);
		if (strlen(v)>0)
		  v[strlen(v)-1]=0;
	      }
	  }
	if (e->IsCrossRef == 0 && 
	    e->NewEntryTag != NULL &&
	    strcmp(e->EntryTag,e->NewEntryTag) != 0)
	  { /* print old and new tags out if they are different */
	    fprintf(MessageFile,
		    "Bibtag: %s ==> %s\n",
		    e->EntryTag,
		    e->NewEntryTag);
	    /* now actually do replacement */
	    free(e->EntryTag);
	    e->EntryTag = strdup(e->NewEntryTag);
	  }
      }
  }
}

void PrintDataBase()
{ int i;
  ReplaceTags();
  SortEntries();
  if (InitialText != NULL)
    fprintf(OutputFile,"%s",InitialText);
  if (Preamble != NULL)
    PrintEntry(Preamble);
  for (i=0;i<NumberOfStrings;i++)
    PrintEntry(StringArray[i]);
  for (i=0;i<NumberOfEntries;i++)
    PrintEntry(EntryArray[i]);
  fprintf(OutputFile,"\n");
}

/* main: parse options and do main processing loop.
 */
int main(argc,argv)
int argc;
char **argv;
{ int i,j;
  struct Entry *e;
  MessageFile = stderr;
  SaveOldTags = FALSE;
  InputFileName[0] = 0;
  InputFile = NULL;
  OutputFileName[0] = 0;
  OutputFile = stdout;
  ParseAndExecuteCommandLine(argc,argv);
  PrintDataBase();
  fprintf(MessageFile,"\nBibtag: done (%d strings, %d entries).\n",
	  NumberOfStrings,NumberOfEntries);
  return 0;
}
