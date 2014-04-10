/********************************************************************\
 * SplitLedger.h -- split ledger api                                *
 * Copyright (C) 1998-2000 Linas Vepstas                            *
 *                                                                  *
 * This program is free software; you can redistribute it and/or    *
 * modify it under the terms of the GNU General Public License as   *
 * published by the Free Software Foundation; either version 2 of   *
 * the License, or (at your option) any later version.              *
 *                                                                  *
 * This program is distributed in the hope that it will be useful,  *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    *
 * GNU General Public License for more details.                     *
 *                                                                  *
 * You should have received a copy of the GNU General Public License*
 * along with this program; if not, contact:                        *
 *                                                                  *
 * Free Software Foundation           Voice:  +1-617-542-5942       *
 * 59 Temple Place - Suite 330        Fax:    +1-617-542-2652       *
 * Boston, MA  02111-1307,  USA       gnu@gnu.org                   *
 *                                                                  *
\********************************************************************/

#ifndef __XACC_SPLIT_LEDGER_H__
#define __XACC_SPLIT_LEDGER_H__

#include "gnc-common.h"
#include "Group.h"
#include "splitreg.h"
#include "Transaction.h"


/* Callback function type */
typedef gncUIWidget (*SRGetParentCallback) (void *user_data);
typedef void (*SRSetHelpCallback) (void *user_data, const char *help_str);
typedef gboolean (*SRReverseBalanceCallback) (Account *account);


/* The xaccSRSetData() method sets the user data and callback
 *    hooks for the register. */
void xaccSRSetData(SplitRegister *reg, void *user_data,
                   SRGetParentCallback get_parent,
                   SRSetHelpCallback set_help);


/* The xaccSRSetAccountSeparator() method sets the character
 *    used to separate accounts in fully-qualified names. */
void xaccSRSetAccountSeparator(char separator);

/* The xaccSRSetReverseBalanceCallback() method sets up
 *    a callback used to determine whether split balances
 *    should be reversed. */
void xaccSRSetReverseBalanceCallback(SRReverseBalanceCallback callback);

/* The xaccSRGetCurrentTrans() method returns the transaction
 *    which is the parent of the current split (see below). */
Transaction * xaccSRGetCurrentTrans (SplitRegister *reg);

/* The xaccSRGetCurrentSplit() method returns the split at which 
 *    the cursor is currently located. */
Split * xaccSRGetCurrentSplit (SplitRegister *reg);

/* The xaccSRGetBlankSplit() method returns the blank split or
 *    NULL if there is none. */
Split * xaccSRGetBlankSplit (SplitRegister *reg);

/* The xaccSRGetSplitVirtLoc() method searches the split register for
 *    the given split. If found, it returns TRUE and vcell_loc
 *    is set to the location of the split. Otherwise, the method
 *    returns FALSE. */
gboolean xaccSRGetSplitVirtLoc (SplitRegister *reg, Split *split,
                                VirtualCellLocation *vcell_loc);

/* The xaccSRGetSplitAmountVirtLoc() method searches the split
 *    register for the given split. If found, it returns TRUE and
 *    virt_loc is set to the location of either the debit or credit
 *    column in the split, whichever one is non-blank. Otherwise,
 *    the method returns FALSE. */
gboolean xaccSRGetSplitAmountVirtLoc (SplitRegister *reg, Split *split,
                                      VirtualLocation *virt_loc);

/* The xaccSRDuplicateCurrent() method duplicates either the current
 *    transaction or the current split depending on the register mode
 *    and cursor position. Returns the split just created, or the
 *    'main' split of the transaction just created, or NULL if
 *    nothing happened. */
Split * xaccSRDuplicateCurrent (SplitRegister *reg);

/* The xaccSRCopyCurrent() method makes a copy of the current entity,
 *    either a split or a transaction, so that it can be pasted later. */
void    xaccSRCopyCurrent  (SplitRegister *reg);

/* The xaccSRCutCurrent() method is equivalent to copying the current
 *    entity and the deleting it with the approriate delete method. */
void    xaccSRCutCurrent   (SplitRegister *reg);

/* The xaccSRPasteCurrent() method pastes a previous copied entity
 *    onto the current entity, but only if the copied entity and the
 *    current entity are of the same type. */
void    xaccSRPasteCurrent (SplitRegister *reg);

/* The xaccSRDeleteCurrentSplit() method deletes the split associated
 *    with the current cursor, if both are non-NULL. If successful, all
 *    affected account windows are refreshed. Deleting the blank split
 *    just clears the cursor values. */
void    xaccSRDeleteCurrentSplit (SplitRegister *reg);

/* The xaccSRDeleteCurrentTrans() method deletes the transaction
 *    associated with the current cursor, if both are non-NULL.
 *    If successful, all affected account windows are refreshed. */
void    xaccSRDeleteCurrentTrans (SplitRegister *reg);

/* The xaccSREmptyCurrentTrans() method deletes the non-transaction
 *    splits associated wih the current cursor, if both are non-NULL.
 *    If successful, all affected account windows are refreshed. */
void    xaccSREmptyCurrentTrans  (SplitRegister *reg);

/* The xaccSRCancelCursorSplitChanges() method cancels any changes made
 *    to the current cursor, reloads the cursor from the engine, reloads
 *    the table from the cursor, and updates the GUI. The change flags
 *    are cleared. */
void    xaccSRCancelCursorSplitChanges (SplitRegister *reg);

/* The xaccSRCancelCursorTransChanges() method cancels any changes made
 *    to the current pending transaction, reloads the table from the engine,
 *    and updates the GUI. The change flags are cleared. */
void    xaccSRCancelCursorTransChanges (SplitRegister *reg);

/* The xaccSRLoadRegister() subroutine will copy transaction
 *    information from a list of splits to the rows of the
 *    register GUI.  The third argument, default_source_acc,
 *    will be used to initialize the source account of a new,
 *    blank split appended to the tail end of the register.
 *    This "blank split" is the place where the user can 
 *    edit info to create new tranasactions. */
void    xaccSRLoadRegister (SplitRegister *reg, Split **slist,
                            Account *default_source_acc);

/* The xaccSRSaveRegEntry() method will copy the contents 
 *    from the cursor to a split.  The split/transaction
 *    that is updated is the one associated with the current 
 *    cursor (register entry) position. If the do_commit flag
 *    is set, the transaction will also be committed. If it is
 *    the blank transaction, and the do_commit flag is set,
 *    a refresh will result in a new blank transaction.
 *    The method returns TRUE if something was changed. */
gboolean xaccSRSaveRegEntry (SplitRegister *reg, gboolean do_commit);

/* The xaccSRRedrawRegEntry() method should be called soon 
 *    after the xaccSRSaveRegEntry() method.  It checks the 
 *    change flag for the current entry/split, and if it
 *    has been modified, it causes a redraw of any register
 *    window that could be affected.  That is, it causes 
 *    a redraw of any window showing this split, or any
 *    other split that belongs to this same tansaction. */
void     xaccSRRedrawRegEntry (SplitRegister *reg);

/* The xaccSRLoadXferCells() method loads (or reloads) the transfer
 *    cells with appropriate entries. */
void     xaccSRLoadXferCells (SplitRegister *reg, Account *base_account);

/* The xaccSRHasPendingChanges() method returns TRUE if the register
 *    has changed cells that have not been committed. */
gboolean xaccSRHasPendingChanges (SplitRegister *reg);

#endif /* __XACC_SPLIT_LEDGER_H__ */