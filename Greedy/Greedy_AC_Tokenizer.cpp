/* ******************************************************************** **
** @@ Greedy AC Tokenizer
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  : Tokenize unsyntaxed text by provided wordlist
** ******************************************************************** */

/* ******************************************************************** **
**                uses pre-compiled headers
** ******************************************************************** */

#include "stdafx.h"

#include <stdio.h>
#include <limits.h>

#include "..\shared\mmf.h"
#include "..\shared\vector.h"
#include "..\shared\vector_sorted.h"
#include "..\shared\slist.h"
#include "..\shared\search_ac.h"
#include "..\shared\file_walker.h"
#include "..\shared\text.h"

#ifdef NDEBUG
#pragma optimize("gsy",on)
#pragma comment(linker,"/MERGE:.rdata=.text /MERGE:.data=.text /SECTION:.text,EWR")
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/* ******************************************************************** **
** @@                   internal defines
** ******************************************************************** */

#define MAX_WORD_LEN             (40)
#define EVENT_LIST_SIZE          (65536)
#define EVENT_LIST_DELTA         (32768)

#pragma pack(push,1)
struct EVENT_INFO
{
   DWORD             _dwIdx;
   DWORD             _dwWhat;
   DWORD             _dwWhere;
   char              _pszWord[MAX_WORD_LEN + 1];
};
#pragma pack(pop)

/* ******************************************************************** **
** @@                   internal prototypes
** ******************************************************************** */

/* ******************************************************************** **
** @@                   external global variables
** ******************************************************************** */

extern DWORD   dwKeepError = 0;

extern BYTE*   _pBuf       = NULL;
extern DWORD   _dwSize     = 0;

/* ******************************************************************** **
** @@                   static global variables
** ******************************************************************** */

static MMF              _MF;

static AC_Search*       _pACS = NULL;

static SortedVector*    _pEventList = NULL;

static FILE*            _pOut        = NULL;
static int              _iSigzCnt    = 0;
static DWORD            _dwPrevBreak = 0;

/* ******************************************************************** **
** @@                   real code
** ******************************************************************** */

/* ******************************************************************** **
** @@ EventSorter()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static int EventSorter(const void** const pKey1,const void** const pKey2)
{
   EVENT_INFO*      p1 = *(EVENT_INFO**)pKey1;
   EVENT_INFO*      p2 = *(EVENT_INFO**)pKey2;

   return strcmp(p1->_pszWord,p2->_pszWord);
}

/* ******************************************************************** **
** @@ PrintHeader()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static void PrintHeader()
{
   // ! Should DO NOT contain star char in the second line position !
   fprintf(_pOut,"-*-   Greedy AC Tokenizer 1.0  *  Copyright (c)  Gazlan, 2014   -*-\n\n\n");
}

/* ******************************************************************** **
** @@ PopulateEventList()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static void PopulateEventList(const char* const pszWordlist)
{
   FILE*    pIn = fopen(pszWordlist,"rt");

   if (!pIn)
   {
      perror("\a\nOpen Input File Error !\n");
      return;
   }

   char     pBuf[MAX_PATH + 1];

   while (fgets(pBuf,MAX_PATH,pIn))
   {
      pBuf[MAX_PATH - 1] = 0; // Ensure ASCIIZ !

      DWORD    dwEOL = strcspn(pBuf,"\r\n");

      pBuf[dwEOL] = 0;  // Remove EOL chars

      int   iLen = strlen(pBuf);

      if (iLen > 0)
      {
         EVENT_INFO*    pEvent = new EVENT_INFO;

         if (!pEvent)
         {
            // Error !
            return;
         }

         memset(pEvent,0,sizeof(EVENT_INFO));

         strncpy(pEvent->_pszWord,pBuf,MAX_WORD_LEN);
         pEvent->_pszWord[MAX_WORD_LEN] = 0; // Ensure ASCIIZ

         pEvent->_dwWhat = (DWORD)pEvent->_pszWord;
         pEvent->_dwIdx  = iLen;

         if (_pEventList->Insert(pEvent) == -1)
         {
            // Error !
            printf("Err: Can't insert event [%s]\n",pEvent->_pszWord);
            delete pEvent;
            pEvent = NULL;
         }
      }
   }

   fclose(pIn);
   pIn = NULL;
}

/* ******************************************************************** **
** @@ CleanupEventList()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static void CleanupEventList()
{
   int   iCnt = _pEventList->Count();

   for (int ii = (iCnt - 1); ii >= 0; --ii)
   {
      EVENT_INFO*    pEvent = (EVENT_INFO*)_pEventList->At(ii);

      _pEventList->RemoveAt(ii);

      if (pEvent)
      {
         delete pEvent;
         pEvent = NULL;
      }
   }
}

/* ******************************************************************** **
** @@ DumpEventList()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

#ifdef _DEBUG
static void DumpEventList()
{
   FILE*    pOut = fopen("!!_EventList.dump","wt");

   int   iCnt = _pEventList->Count();

   for (int ii = 0; ii < iCnt; ++ii)
   {
      EVENT_INFO*    pEntry = (EVENT_INFO*)_pEventList->At(ii);

      if (pEntry)
      {
         fprintf(pOut,"%s\n",pEntry->_pszWord);
      }
   }

   fclose(pOut);
   pOut = NULL;
}
#endif

/* ******************************************************************** **
** @@ Finder()
** @  Copyrt :
** @  Author :
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static DWORD __fastcall Finder(void* pFound,DWORD dwFoundAt)
{
   DWORD    dwGapSize = dwFoundAt - _dwPrevBreak;

   fprintf(_pOut," ");

   if (dwGapSize)
   {
      for (DWORD ii = 0; ii < dwGapSize; ++ii)
      {
         fprintf(_pOut,"%c",_pBuf[_dwPrevBreak + ii]);
      }
   
      fprintf(_pOut," ");
   }

   EVENT_INFO*    pEvent = (EVENT_INFO*)pFound;

   _dwPrevBreak = dwFoundAt + pEvent->_dwIdx;

   if (pEvent->_dwIdx)
   {
      for (DWORD ii = 0; ii < pEvent->_dwIdx; ++ii)
      {
         fprintf(_pOut,"%c",_pBuf[dwFoundAt + ii]);
      }
   }

   return pEvent->_dwIdx;
}

/* ******************************************************************** **
** @@ ForEach()
** @  Copyrt : 
** @  Author : 
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

#pragma warning(disable: 4189)
static void ForEach(const char* const pszFileName)
{
   char     pszTemp[MAX_PATH + 1];

   memset(pszTemp,0,sizeof(pszTemp));

   strncpy(pszTemp,pszFileName,MAX_PATH - 3);
   pszTemp[MAX_PATH - 3] = 0; // ASCIIZ
   strcat(pszTemp,".prs");

   _pOut = fopen(pszTemp,"wt");

   if (!_pOut)
   {
      return;
   }

   PrintHeader();

   DWORD    dwTimerStart = GetTickCount();

   if (!_MF.OpenReadOnly(pszFileName))
   {
      fclose(_pOut);
      _pOut = NULL;
      return;
   }

   _pBuf   = _MF.Buffer();
   _dwSize = _MF.Size();

   DWORD    dwStart = 0;

   while (true)
   {
      DWORD    dwFoundAt = 0;

      void*    pFound = _pACS->FindFirstLongest(_pBuf,_dwSize,dwStart,dwFoundAt);

      if (!pFound)
      {
         break;
      }

      int   iShift = Finder(pFound,dwFoundAt);
      
      dwStart = dwFoundAt + max(1,iShift);             
   }

   fprintf(_pOut,"\n");

   _MF.Close();

   _pBuf   = NULL;
   _dwSize = 0;

   DWORD    dwTimerStop = GetTickCount();

   fprintf(_pOut,"---------------\nTime netto, ms: %u\n",dwTimerStop - dwTimerStart);

   fclose(_pOut);
   _pOut = NULL;
}

/* ******************************************************************** **
** @@ PopulateDix()
** @  Copyrt : 
** @  Author : 
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static void PopulateDix()
{
   for (int ii = 0; ii < _iSigzCnt; ++ii)
   {
      EVENT_INFO*    pEvent = (EVENT_INFO*)_pEventList->At(ii);

      if (!pEvent)
      {
         // Error !
         continue;
      }     

      // Trick!
      // Ptr to Array saved instead Trigger
      // Array size saved into Idx field
      // Note: Up to USHORT_MAX Item Size !
      if (!_pACS->AddCase((BYTE*)(pEvent->_dwWhat),(WORD)pEvent->_dwIdx,pEvent))
      {
         #ifdef _DEBUG
//         const CC_ENTRY*      pEntry = FindEntry(pEvent->_dwID);

//         printf("Err: Can't add [ %5d / %08X / %s] signature. Possible duplicate.\n",pEvent->_dwID,pEvent->_dwWhat,GetDescr(pEntry->_dwDescrID));
//         printf("The signature is: [%s]\n\n",(char*)pEvent->_dwWhat);
         #endif
      }
   }    
}

/* ******************************************************************** **
** @@ ShowHelp()
** @  Copyrt : 
** @  Author : 
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

static void ShowHelp()
{
   const char  pszCopyright[] = "-*-   Greedy AC Tokenizer 1.0  *  Copyright (c)  Gazlan, 2014   -*-";
   const char  pszDescript [] = "Tokenize unsyntaxed text by provided wordlist";
   const char  pszE_Mail   [] = "complains_n_suggestions direct to gazlan@yandex.ru";

   printf("%s\n",pszCopyright);
   printf("\n%s\n",pszDescript);
   printf("\nUsage: Greedy_AC_Tokenizer.com Wordlist.txt VictimFile\n");
   printf("\n%s\n\n",pszE_Mail);
}

/* ******************************************************************** **
** @@ main()
** @  Copyrt : 
** @  Author : 
** @  Modify :
** @  Update :
** @  Notes  :
** ******************************************************************** */

int main(int argc,char** argv)
{
   if (argc != 3)
   {
      ShowHelp();
      return 0;
   }

   if ((!strcmp(argv[1],"?")) || (!strcmp(argv[1],"/?")) || (!strcmp(argv[1],"-?")) || (!stricmp(argv[1],"/h")) || (!stricmp(argv[1],"-h")))
   {
      ShowHelp();
      return 0;
   }

   char     pszMask[MAX_PATH + 1];

   memset(pszMask,0,sizeof(pszMask));

   strncpy(pszMask,argv[2],MAX_PATH);
   pszMask[MAX_PATH] = 0; // ASCIIZ

   _pEventList = new SortedVector();

   if (!_pEventList)
   {
      // Error !
      return 0;
   }

   _pEventList->Resize(EVENT_LIST_SIZE);
   _pEventList->Delta(EVENT_LIST_DELTA);
   _pEventList->SetSorter(EventSorter);

   PopulateEventList(argv[1]);

   #ifdef _DEBUG
   DumpEventList();
   #endif

   _iSigzCnt = _pEventList->Count();

   _pACS = new AC_Search;

   if (!_pACS)
   {
      // Error !
      printf("Err: Can't create Search Dictionary.\n");
      CleanupEventList();
      delete _pEventList;
      _pEventList = NULL;
      delete _pACS;
      _pACS = NULL;
      return 0;
   }

   PopulateDix();

   // Finalize
   if (!_pACS->Finalize())
   {
      // Error !
      delete _pACS;
      _pACS = NULL;
      return 0;
   }

   char     pszDrive   [_MAX_DRIVE];
   char     pszDir     [_MAX_DIR];
   char     pszFName   [_MAX_FNAME];
   char     pszExt     [_MAX_EXT];

   _splitpath(pszMask,pszDrive,pszDir,pszFName,pszExt);

   char     pszSrchMask[MAX_PATH + 1];
   char     pszSrchPath[MAX_PATH + 1];

   strcpy(pszSrchMask,pszFName);
   strcat(pszSrchMask,pszExt);

   Walker      Visitor;

   Visitor.Init(ForEach,pszSrchMask,false);

   strcpy(pszSrchPath,pszDrive);
   strcat(pszSrchPath,pszDir);

   Visitor.Run(*pszSrchPath  ?  pszSrchPath  :  ".");

   CleanupEventList();

   delete _pEventList;
   _pEventList = NULL;      

   delete _pACS;
   _pACS = NULL;

   return 0;
}

/* ******************************************************************** **
**                That's All
** ******************************************************************** */
