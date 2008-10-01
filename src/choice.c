/**
 * chewingio.c
 *
 * Copyright (c) 1999, 2000, 2001
 *	Lu-chuan Kung and Kang-pen Chen.
 *	All rights reserved.
 *
 * Copyright (c) 2004, 2005, 2006, 2007
 *	libchewing Core Team. See ChangeLog for details.
 *
 * See the file "COPYING" for information on usage and redistribution
 * of this file.
 */

/**
 * @file choice.c
 * @brief Choice module
 */

#include <string.h>
#include <assert.h>

#include "chewing-definition.h"
#include "chewing-utf8-util.h"
#include "global.h"
#include "dict.h"
#include "char.h"
#include "chewingutil.h"
#include "userphrase.h"

#define CEIL_DIV( a, b ) 	( ( a + b - 1 ) / b )

#ifdef USE_BINARY_DATA
TreeType *tree = NULL;
#else
TreeType tree[ TREE_SIZE ];
#endif

static void ChangeSelectIntervalAndBreakpoint(
		ChewingData *pgdata,
		int from,
		int to,
		char *str )
{
	int i;
	int user_alloc;

	IntervalType inte;

	inte.from = from;
	inte.to = to;
	for ( i = 0; i < pgdata->nSelect; i++ ) {
		if ( IsIntersect( inte, pgdata->selectInterval[ i ] ) ) {
			RemoveSelectElement( i, pgdata );
			i--;
		}
	}

	pgdata->selectInterval[ pgdata->nSelect ].from = from;
	pgdata->selectInterval[ pgdata->nSelect ].to = to;

	/* No available selection */
	if ( ( user_alloc = ( to - from ) ) == 0 )
		return;

	ueStrNCpy( pgdata->selectStr[ pgdata->nSelect ],
			str,
			user_alloc, 1);
	pgdata->nSelect++;

	if ( user_alloc > 1 ) {
		memset( &pgdata->bUserArrBrkpt[ from + 1 ], 0, sizeof( int ) * ( user_alloc - 1 ) );
		memset( &pgdata->bUserArrCnnct[ from + 1 ], 0, sizeof( int ) * ( user_alloc - 1 ) );
	}
}

/** @brief Loading all possible phrases after the cursor from long to short into AvailInfo structure.*/
static void SetAvailInfo( 
		AvailInfo *pai, const uint16 phoneSeq[], 
		int nPhoneSeq, int begin, const int bSymbolArrBrkpt[] )
{
	int end, pho_id;
	int diff;
	uint16 userPhoneSeq[ MAX_PHONE_SEQ_LEN ];

	pai->nAvail = 0;

	for ( end = begin; end < nPhoneSeq; end++ ) {
		diff = end - begin;
		if ( diff > 0 && bSymbolArrBrkpt[ end ] ) 
			break;

		pho_id = TreeFindPhrase( begin, end, phoneSeq );
		if ( pho_id != -1 ) {
			/* save it! */
			pai->avail[ pai->nAvail ].len = diff + 1;
			pai->avail[ pai->nAvail ].id = pho_id;
			pai->nAvail++;
		}
		else {
			memcpy(
				userPhoneSeq, 
				&phoneSeq[ begin ], 
				sizeof( uint16 ) * ( diff + 1 ) ) ;
			userPhoneSeq[ diff + 1 ] = 0;
			if ( UserGetPhraseFirst( userPhoneSeq ) ) {
				/* save it! */
				pai->avail[ pai->nAvail ].len = diff + 1;
				pai->avail[ pai->nAvail ].id = -1;
				pai->nAvail++;
			} else {
				pai->avail[ pai->nAvail ].len = 0;
				pai->avail[ pai->nAvail ].id = -1;
			}
		}
	}
}

static int ChoiceTheSame( ChoiceInfo *pci, char *str, int len )
{
	int i;

	for ( i = 0; i < pci->nTotalChoice; i++ )
		if ( ! memcmp( pci->totalChoiceStr[ i ], str, len ) ) 
			return 1;
	return 0;
}

/** @brief Loading all possible phrases of certain length.
 *
 *	   Loading all possible phrases of certain length into ChoiceInfo structure from static
 *	   and dynamic dictionaries,\n
 *	   including number of total pages and the number of current page.\n
 */
static void SetChoiceInfo(
		ChoiceInfo *pci,AvailInfo *pai, uint16 *phoneSeq, int cursor,
		int candPerPage )
{
	Word tempWord;
	Phrase tempPhrase;
	int len;
	UserPhraseData *pUserPhraseData;
	uint16 userPhoneSeq[ MAX_PHONE_SEQ_LEN ];
	
	// Clears previous candidates.
	memset( pci->totalChoiceStr, '\0', sizeof(char) * MAX_CHOICE * MAX_PHRASE_LEN * MAX_UTF8_SIZE + 1);

	pci->nTotalChoice = 0;
	len = pai->avail[ pai->currentAvail ].len;

	/* secondly, read tree phrase */
	if ( len == 1 ) { /* single character */
		GetCharFirst( &tempWord, phoneSeq[ cursor ] );
		do {
			if ( ChoiceTheSame( pci, tempWord.word, ueBytesFromChar( tempWord.word[0] ) * sizeof( char ) ) ) 
				continue;
			memcpy( 
				pci->totalChoiceStr[ pci->nTotalChoice ],
				tempWord.word, ueBytesFromChar( tempWord.word[0] ) * sizeof( char ) );
			assert(pci->nTotalChoice <= MAX_CHOICE);
			pci->totalChoiceStr[ pci->nTotalChoice ][ ueBytesFromChar( tempWord.word[0] ) ] = '\0';
			pci->nTotalChoice++;
		} while( GetCharNext( &tempWord ) );
	}
	/* phrase */
	else {
		if ( pai->avail[ pai->currentAvail ].id != -1 ) {
			GetPhraseFirst( &tempPhrase, pai->avail[ pai->currentAvail ].id );
			do {
				if ( ChoiceTheSame( 
					pci, 
					tempPhrase.phrase, 
					len * ueBytesFromChar( tempPhrase.phrase[0] ) * sizeof( char ) ) ) {
					continue;
				}
				ueStrNCpy( pci->totalChoiceStr[ pci->nTotalChoice ],
						tempPhrase.phrase, len, 1);
				pci->nTotalChoice++;
			} while( GetPhraseNext( &tempPhrase ) );
		}

		memcpy( userPhoneSeq, &phoneSeq[ cursor ], sizeof( uint16 ) * len );
		userPhoneSeq[ len ] = 0;
		pUserPhraseData = UserGetPhraseFirst( userPhoneSeq );
		if ( pUserPhraseData ) {
			do {
				/* check if the phrase is already in the choice list */
				if ( ChoiceTheSame( 
					pci, 
					pUserPhraseData->wordSeq, 
					len * ueBytesFromChar( pUserPhraseData->wordSeq[0] ) * sizeof( char ) ) )
					continue;
				/* otherwise store it */
				ueStrNCpy(
						pci->totalChoiceStr[ pci->nTotalChoice ],
						pUserPhraseData->wordSeq,
						len, 1);
				pci->nTotalChoice++;
			} while( ( pUserPhraseData = 
				UserGetPhraseNext( userPhoneSeq ) ) != NULL );
		}

	}

	/* magic number */
	pci->nChoicePerPage = candPerPage;
	if ( pci->nChoicePerPage > MAX_SELKEY )
		pci->nChoicePerPage = MAX_SELKEY;
	pci->nPage = CEIL_DIV( pci->nTotalChoice, pci->nChoicePerPage );
	pci->pageNo = 0;
}

/** @brief Enter choice mode and relating initialisations. */
int ChoiceFirstAvail( ChewingData *pgdata )
{
	/* save old cursor position */
	pgdata->choiceInfo.oldCursor = pgdata->cursor;
	pgdata->choiceInfo.oldChiSymbolCursor = pgdata->chiSymbolCursor;

	/* see if there is some word in the cursor position */
	if ( pgdata->nPhoneSeq == pgdata->cursor )
		pgdata->cursor = pgdata->phrOut.dispInterval[ pgdata->phrOut.nDispInterval - 1 ].from;
	if ( pgdata->chiSymbolBufLen == pgdata->chiSymbolCursor )
		pgdata->chiSymbolCursor = pgdata->phrOut.dispInterval[ pgdata->phrOut.nDispInterval - 1 ].from;

	pgdata->bSelect = 1;

	SetAvailInfo( 
		&( pgdata->availInfo ), 
		pgdata->phoneSeq, 
		pgdata->nPhoneSeq,
		pgdata->cursor, 
		pgdata->bSymbolArrBrkpt );
	pgdata->availInfo.currentAvail = pgdata->availInfo.nAvail - 1;
	SetChoiceInfo(
		&( pgdata->choiceInfo ), 
		&( pgdata->availInfo ), 
		pgdata->phoneSeq, 
		pgdata->cursor, 
		pgdata->config.candPerPage );
	return 0;
}

int ChoicePrevAvail( ChewingData *pgdata )
{
	if (pgdata->choiceInfo.isSymbol) return 0;
	if ( ++( pgdata->availInfo.currentAvail ) >= pgdata->availInfo.nAvail )
		pgdata->availInfo.currentAvail = 0;
	SetChoiceInfo( 
		&( pgdata->choiceInfo ), 
		&( pgdata->availInfo ), 
		pgdata->phoneSeq, 
		pgdata->cursor,
		pgdata->config.candPerPage );
	return 0;
}

/** @brief Return the next phrase not longer than the previous phrase. */
int ChoiceNextAvail( ChewingData *pgdata ) 
{
	if (pgdata->choiceInfo.isSymbol) return 0;
	if ( --( pgdata->availInfo.currentAvail ) < 0 )
		pgdata->availInfo.currentAvail = pgdata->availInfo.nAvail - 1;
	SetChoiceInfo(
		&( pgdata->choiceInfo ), 
		&( pgdata->availInfo ), 
		pgdata->phoneSeq,pgdata->cursor,
		pgdata->config.candPerPage );
	return 0;
}

int ChoiceEndChoice( ChewingData *pgdata )
{
	pgdata->bSelect = 0;
	pgdata->choiceInfo.nTotalChoice = 0;
	pgdata->choiceInfo.nPage = 0;

	if ( pgdata->choiceInfo.isSymbol != 1 || pgdata->choiceInfo.isSymbol != 2 ) {
		/* return to the old cursor & chiSymbolCursor position */
		pgdata->cursor = pgdata->choiceInfo.oldCursor;
		pgdata->chiSymbolCursor = pgdata->choiceInfo.oldChiSymbolCursor;
	}
	pgdata->choiceInfo.isSymbol = 0;
	return 0;
}

static void ChangeUserData( ChewingData *pgdata, int selectNo )
{
	uint16 userPhoneSeq[ MAX_PHONE_SEQ_LEN ];
	int len;

	len = ueStrLen( pgdata->choiceInfo.totalChoiceStr[ selectNo ] ); 
	memcpy(
		userPhoneSeq, 
		&( pgdata->phoneSeq[ pgdata->cursor ] ), 
		len * sizeof( uint16 ) );
	userPhoneSeq[ len ] = 0;
	UserUpdatePhrase( userPhoneSeq, pgdata->choiceInfo.totalChoiceStr[ selectNo ] );
}

/** @brief commit the selected phrase. */
int ChoiceSelect( ChewingData *pgdata, int selectNo ) 
{
	ChoiceInfo *pci = &( pgdata->choiceInfo );
	AvailInfo *pai = &( pgdata->availInfo );

	ChangeUserData( pgdata, selectNo );
	ChangeSelectIntervalAndBreakpoint(
			pgdata,
			pgdata->cursor,
			pgdata->cursor + pai->avail[ pai->currentAvail ].len,
			pci->totalChoiceStr[ selectNo ] );
	ChoiceEndChoice( pgdata );
	return 0;
}

