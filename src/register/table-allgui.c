/********************************************************************\
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

/*
 * FILE:
 * table-allgui.c
 *
 * FUNCTION:
 * Implements the gui-independent parts of the table infrastructure.
 *
 * HISTORY:
 * Copyright (c) 1998,1999,2000 Linas Vepstas
 * Copyright (c) 2000 Dave Peticolas
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "table-allgui.h"
#include "cellblock.h"
#include "util.h"


/** Datatypes **********************************************************/

/* The generic TableCell is used for memory allocation purposes. */
typedef union _TableCell TableCell;
union _TableCell
{
  VirtualCell virt_cell;
  PhysicalCell phys_cell;
};


/** Static Globals *****************************************************/

/* This static indicates the debugging module that this .o belongs to. */
static short module = MOD_REGISTER;

static GMemChunk *cell_mem_chunk = NULL;


/** Prototypes *********************************************************/
static void gnc_table_init (Table * table);
static void gnc_table_free_data (Table * table);
static gpointer gnc_virtual_cell_new (void);
static gpointer gnc_physical_cell_new (void);
static void gnc_virtual_cell_free (gpointer tcell);
static void gnc_physical_cell_free (gpointer tcell);
static void gnc_table_resize (Table * table,
                              int new_phys_rows, int new_phys_cols,
                              int new_virt_rows, int new_virt_cols);


/** Implementation *****************************************************/

Table * 
gnc_table_new (void)
{
   Table *table;

   /* Do this here for lower overhead. */
   if (cell_mem_chunk == NULL)
     cell_mem_chunk = g_mem_chunk_create(TableCell, 2048, G_ALLOC_AND_FREE);

   table = g_new0(Table, 1);

   gnc_table_init (table);

   table->virt_cells = g_table_new(gnc_virtual_cell_new,
                                   gnc_virtual_cell_free);

   table->phys_cells = g_table_new(gnc_physical_cell_new,
                                   gnc_physical_cell_free);

   return table;
}

/* ==================================================== */

static void
gnc_table_init (Table * table)
{
   table->num_phys_rows = -1;
   table->num_phys_cols = -1;
   table->num_virt_rows = -1;
   table->num_virt_cols = -1;

   table->current_cursor = NULL;
   table->current_cursor_virt_loc.virt_row = -1;
   table->current_cursor_virt_loc.virt_col = -1;
   table->current_cursor_phys_loc.phys_row = -1;
   table->current_cursor_phys_loc.phys_col = -1;

   table->move_cursor = NULL;
   table->traverse = NULL;
   table->set_help = NULL;
   table->user_data = NULL;

   table->alternate_bg_colors = FALSE;

   /* initialize private data */

   table->virt_cells = NULL;
   table->phys_cells = NULL;

   table->ui_data = NULL;
   table->destroy = NULL;
}

/* ==================================================== */

void 
gnc_table_destroy (Table * table)
{
   /* invoke destroy callback */
   if (table->destroy)
     table->destroy(table);

   /* free the dynamic structures */
   gnc_table_free_data (table);

   /* free the cell tables */
   g_table_destroy(table->virt_cells);
   g_table_destroy(table->phys_cells);

   /* intialize vars to null value so that any access is voided. */
   gnc_table_init (table);

   g_free (table);
}

/* ==================================================== */

VirtualCell *
gnc_table_get_virtual_cell (Table *table, VirtualCellLocation vcell_loc)
{
  TableCell *tcell;

  if (table == NULL)
    return NULL;

  tcell = g_table_index (table->virt_cells,
                         vcell_loc.virt_row, vcell_loc.virt_col);

  if (tcell == NULL)
    return NULL;

  return &tcell->virt_cell;
}

/* ==================================================== */

PhysicalCell *
gnc_table_get_physical_cell (Table *table, PhysicalLocation phys_loc)
{
  TableCell *tcell;

  if (table == NULL)
    return NULL;

  tcell = g_table_index (table->phys_cells,
                         phys_loc.phys_row, phys_loc.phys_col);

  if (tcell == NULL)
    return NULL;

  return &tcell->phys_cell;
}

/* ==================================================== */

VirtualCell *
gnc_table_get_header_cell (Table *table)
{
  VirtualCellLocation vcell_loc = { 0, 0 };

  return gnc_table_get_virtual_cell (table, vcell_loc);
}

/* ==================================================== */

void 
gnc_table_set_size (Table * table,
                    int phys_rows, int phys_cols,
                    int virt_rows, int virt_cols)
{
   /* Invalidate the current cursor position, if the array is
    * shrinking. This must be done since the table is probably
    * shrinking because some rows were deleted, and there's a pretty
    * good chance (100% with current design) that the cursor is
    * located on the deleted rows.  */
   if ((virt_rows < table->num_virt_rows) ||
       (virt_cols < table->num_virt_cols) ||
       (phys_rows < table->num_phys_rows) ||
       (phys_cols < table->num_phys_cols)) 
   {
      CellBlock *curs;

      table->current_cursor_virt_loc.virt_row = -1;
      table->current_cursor_virt_loc.virt_col = -1;
      table->current_cursor_phys_loc.phys_row = -1;
      table->current_cursor_phys_loc.phys_col = -1;

      curs = table->current_cursor;

      if (curs)
        curs->user_data = NULL;

      table->current_cursor = NULL;
   }

   gnc_table_resize (table, phys_rows, phys_cols, virt_rows, virt_cols);
}

/* ==================================================== */

static void
gnc_table_free_data (Table * table)
{
  if (table == NULL)
    return;

  g_table_resize (table->virt_cells, 0, 0);
  g_table_resize (table->phys_cells, 0, 0);
}

/* ==================================================== */

static void
gnc_virtual_location_init (VirtualLocation *vloc)
{
  if (vloc == NULL)
    return;

  vloc->phys_row_offset = -1;
  vloc->phys_col_offset = -1;
  vloc->vcell_loc.virt_row = -1;
  vloc->vcell_loc.virt_col = -1;
}

/* ==================================================== */

static void
gnc_physical_location_init (PhysicalLocation *ploc)
{
  if (ploc == NULL)
    return;

  ploc->phys_row = -1;
  ploc->phys_col = -1;
}

/* ==================================================== */

static gpointer
gnc_virtual_cell_new (void)
{
  TableCell *tcell;
  VirtualCell *vcell;

  tcell = g_chunk_new(TableCell, cell_mem_chunk);

  vcell = &tcell->virt_cell;

  vcell->cellblock = NULL;
  vcell->user_data = NULL;

  gnc_physical_location_init(&vcell->phys_loc);

  return tcell;
}

/* ==================================================== */

static void
gnc_virtual_cell_free (gpointer tcell)
{
  if (tcell == NULL)
    return;

  g_mem_chunk_free(cell_mem_chunk, tcell);
}

/* ==================================================== */

static gpointer
gnc_physical_cell_new (void)
{
  TableCell *tcell;
  PhysicalCell *pcell;

  tcell = g_chunk_new(TableCell, cell_mem_chunk);

  pcell = &tcell->phys_cell;

  pcell->entry = g_strdup("");
  assert(pcell->entry != NULL);

  pcell->fg_color = 0x000000; /* black */
  pcell->bg_color = 0xffffff; /* white */

  gnc_virtual_location_init(&pcell->virt_loc);

  return tcell;
}

/* ==================================================== */

static void
gnc_physical_cell_free (gpointer _tcell)
{
  TableCell *tcell = _tcell;

  if (tcell == NULL)
    return;

  g_free(tcell->phys_cell.entry);
  tcell->phys_cell.entry = NULL;

  g_mem_chunk_free(cell_mem_chunk, tcell);
}

/* ==================================================== */

static void 
gnc_table_resize (Table * table,
                  int new_phys_rows, int new_phys_cols,
                  int new_virt_rows, int new_virt_cols)
{
  if (!table) return;

  if ((new_phys_rows < new_virt_rows) ||
      (new_phys_cols < new_virt_cols))
  {
    FATAL ("xaccTableResize(): the number of physical rows (%d %d)"
           "must equal or exceed the number of virtual rows (%d %d)\n",
           new_phys_rows, new_phys_cols,
           new_virt_rows, new_virt_cols);
    exit (1);
  }

  g_table_resize (table->virt_cells, new_virt_rows, new_virt_cols);
  g_table_resize (table->phys_cells, new_phys_rows, new_phys_cols);

  table->num_phys_rows = new_phys_rows;
  table->num_phys_cols = new_phys_cols;

  table->num_virt_rows = new_virt_rows;
  table->num_virt_cols = new_virt_cols;
}

/* ==================================================== */

void
gnc_table_set_cursor (Table *table, CellBlock *curs,
                      PhysicalLocation phys_origin,
                      VirtualCellLocation vcell_loc)
{
  VirtualCell *vcell;
  int cell_row, cell_col;

  if ((table == NULL) || (curs == NULL))
    return;

  if (gnc_table_physical_cell_out_of_bounds (table, phys_origin))
    return;

  vcell = gnc_table_get_virtual_cell (table, vcell_loc);
  if (vcell == NULL)
    return;

  /* this cursor is the handler for this block */
  vcell->cellblock = curs;

  vcell->phys_loc = phys_origin;

  /* intialize the mapping so that we will be able to find
   * the handler, given this range of physical cell addresses */
  for (cell_row = 0; cell_row < curs->num_rows; cell_row++)
    for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
    {
      PhysicalCell *pcell;
      PhysicalLocation ploc = { phys_origin.phys_row + cell_row,
                                phys_origin.phys_col + cell_col };

      pcell = gnc_table_get_physical_cell (table, ploc);

      pcell->virt_loc.phys_row_offset = cell_row;
      pcell->virt_loc.phys_col_offset = cell_col;
      pcell->virt_loc.vcell_loc = vcell_loc;
    }
}

/* ==================================================== */

/* Change the cell background colors to their "passive" values.  This
 * denotes that the cursor has left this location (which means more or
 * less the same thing as "the current location is no longer being
 * edited.") */
static void 
gnc_table_make_passive (Table *table)
{
  CellBlock *curs;
  PhysicalCell *pcell;
  PhysicalLocation phys_origin;
  int cell_row, cell_col;
  int virt_row;

  pcell = gnc_table_get_physical_cell (table, table->current_cursor_phys_loc);
  if (pcell == NULL)
    return;

  phys_origin = table->current_cursor_phys_loc;
  phys_origin.phys_row -= pcell->virt_loc.phys_row_offset;
  phys_origin.phys_col -= pcell->virt_loc.phys_col_offset;

  virt_row = pcell->virt_loc.vcell_loc.virt_row;

  curs = table->current_cursor;

  for (cell_row=0; cell_row < curs->num_rows; cell_row++)
    for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
    {
      PhysicalLocation ploc = { phys_origin.phys_row + cell_row,
                                phys_origin.phys_col + cell_col };
      CellBlockCell *cb_cell;
      guint32 color;

      pcell = gnc_table_get_physical_cell (table, ploc);

      if (table->alternate_bg_colors)
      {
        if ((virt_row % 2) == 1)
          color = curs->passive_bg_color;
        else
          color = curs->passive_bg_color2;
      }
      else if (cell_row == 0)
        color = curs->passive_bg_color;
      else
        color = curs->passive_bg_color2;

      pcell->bg_color = color;

      cb_cell = gnc_cellblock_get_cell (curs, cell_row, cell_col);
      if (cb_cell && cb_cell->cell)
      {
        BasicCell *cell = cb_cell->cell;

        if (cell->use_bg_color)
          pcell->bg_color = cell->bg_color;
        if (cb_cell->cell->use_fg_color)
          pcell->fg_color = cell->fg_color;
      }
    }
}


/* ==================================================== */

static void 
gnc_table_move_cursor_internal (Table *table,
                                PhysicalLocation new_phys_loc,
                                gboolean do_move_gui)
{
  int cell_row, cell_col;
  PhysicalLocation phys_origin;
  VirtualCellLocation new_vcell_loc;
  PhysicalCell *pcell;
  VirtualCell *vcell;
  CellBlock *curs;

  ENTER("new_phys=(%d %d) do_move_gui=%d\n", 
        new_phys_loc.phys_row, new_phys_loc.phys_col, do_move_gui);

  /* Change the cell background colors to their "passive" values.
   * This denotes that the cursor has left this location (which means
   * more or less the same thing as "the current location is no longer
   * being edited.") */
  gnc_table_make_passive (table);

  /* call the callback, allowing the app to commit any changes
   * associated with the current location of the cursor. Note that
   * this callback may recursively call this routine. */
  if (table->move_cursor)
  {
    (table->move_cursor) (table, &new_phys_loc);

    /* The above callback can cause this routine to be called
     * recursively. As a result of this recursion, the cursor may
     * have gotten repositioned. We need to make sure we make
     * passive again. */
    gnc_table_make_passive (table);
    if (do_move_gui)
      gnc_table_refresh_current_cursor_gui (table, FALSE);
  }

  pcell = gnc_table_get_physical_cell (table, new_phys_loc);
  if (pcell == NULL)
  {
    new_vcell_loc.virt_row = -1;
    new_vcell_loc.virt_col = -1;
  }
  else
  {
    new_vcell_loc = pcell->virt_loc.vcell_loc;
  }

  /* invalidate the cursor for now; we'll fix it back up below */
  table->current_cursor_phys_loc.phys_row = -1;
  table->current_cursor_phys_loc.phys_col = -1;
  table->current_cursor_virt_loc.virt_row = -1;
  table->current_cursor_virt_loc.virt_col = -1;

  curs = table->current_cursor;
  if (curs)
    curs->user_data = NULL;

  table->current_cursor = NULL;

  /* check for out-of-bounds conditions (which may be deliberate) */
  if ((0 > new_vcell_loc.virt_row) || (0 > new_vcell_loc.virt_col))
  {
    /* if the location is invalid, then we should take this 
     * as a command to unmap the cursor gui.  So do it .. */
    if (do_move_gui && curs)
    {
      for (cell_row = 0; cell_row < curs->num_rows; cell_row++)
        for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
        {
          CellBlockCell *cb_cell;

          cb_cell = gnc_cellblock_get_cell (curs, cell_row, cell_col);
          if (cb_cell && cb_cell->cell)
          {
            BasicCell *cell = cb_cell->cell;

            cell->changed = 0;
            if (cell->move)
            {
              PhysicalLocation ploc = { -1, -1 };
              (cell->move) (cell, ploc);
            }
          }
        }
    }
    LEAVE("out of bounds\n");
    return;
  }

  if (gnc_table_virtual_cell_out_of_bounds (table, new_vcell_loc))
    return;
  if (gnc_table_physical_cell_out_of_bounds (table, new_phys_loc))
    return;

  /* ok, we now have a valid position.  Find the new cursor to use,
   * and initialize its cells */
  vcell = gnc_table_get_virtual_cell (table, new_vcell_loc);
  curs = vcell->cellblock;
  table->current_cursor = curs;

  /* record the new position */
  table->current_cursor_virt_loc = new_vcell_loc;
  table->current_cursor_phys_loc = new_phys_loc;

  /* compute some useful offsets */
  phys_origin = new_phys_loc;

  phys_origin.phys_row -= pcell->virt_loc.phys_row_offset;
  phys_origin.phys_col -= pcell->virt_loc.phys_col_offset;

  /* update the cell values to reflect the new position */
  for (cell_row = 0; cell_row < curs->num_rows; cell_row++)
    for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
    {
      CellBlockCell *cb_cell;
      PhysicalLocation ploc = { phys_origin.phys_row + cell_row,
                                phys_origin.phys_col + cell_col };

      pcell = gnc_table_get_physical_cell (table, ploc);

      /* change the cursor row to the active color */
      pcell->bg_color = curs->active_bg_color;

      cb_cell = gnc_cellblock_get_cell(curs, cell_row, cell_col);
      if (cb_cell && cb_cell->cell)
      {
        BasicCell *cell = cb_cell->cell;

        /* if a cell has a GUI, move that first, before setting
         * the cell value.  Otherwise, we'll end up putting the 
         * new values in the old cell locations, and that would 
         * lead to confusion of all sorts. */
        if (do_move_gui && cell->move)
          (cell->move) (cell, ploc);

        /* OK, now copy the string value from the table at large 
         * into the cell handler. */
        if (XACC_CELL_ALLOW_SHADOW & (cell->input_output))
        {
          xaccSetBasicCellValue (cell, pcell->entry);
          cell->changed = 0;
        }

        if (cell->use_bg_color)
          pcell->bg_color = cell->bg_color;
        if (cell->use_fg_color)
          pcell->fg_color = cell->fg_color;
      }
    }

  curs->user_data = vcell->user_data;

  LEAVE("did move\n");
}

/* ==================================================== */

void
gnc_table_move_cursor (Table *table, PhysicalLocation new_phys_loc)
{
  if (!table) return;

  gnc_table_move_cursor_internal (table, new_phys_loc, FALSE);
}

/* same as above, but be sure to deal with GUI elements as well */
void
gnc_table_move_cursor_gui (Table *table, PhysicalLocation new_phys_loc)
{
  if (!table) return;

  gnc_table_move_cursor_internal (table, new_phys_loc, TRUE);
}

/* ==================================================== */

void
gnc_table_commit_cursor (Table *table)
{
  CellBlock *curs;
  VirtualCell *vcell;
  PhysicalCell *pcell;
  int cell_row, cell_col;
  VirtualCellLocation vcell_loc;
  PhysicalLocation phys_loc;
  PhysicalLocation phys_origin;

  if (!table) return;

  curs = table->current_cursor;
  if (!curs) return;

  vcell_loc = table->current_cursor_virt_loc;

  /* can't commit if cursor is bad */
  if (gnc_table_virtual_cell_out_of_bounds (table, vcell_loc))
    return;

  /* compute the true origin of the cell block */
  phys_loc = table->current_cursor_phys_loc;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL) return;

  phys_origin = table->current_cursor_phys_loc;
  phys_origin.phys_row -= pcell->virt_loc.phys_row_offset;
  phys_origin.phys_col -= pcell->virt_loc.phys_col_offset;

  for (cell_row = 0; cell_row < curs->num_rows; cell_row++)
    for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
    {
      CellBlockCell *cb_cell;
      PhysicalLocation ploc = { phys_origin.phys_row + cell_row,
                                phys_origin.phys_col + cell_col };

      cb_cell = gnc_cellblock_get_cell(curs, cell_row, cell_col);
      if (cb_cell && cb_cell->cell)
      {
        BasicCell *cell = cb_cell->cell;

        pcell = gnc_table_get_physical_cell (table, ploc);

        g_free (pcell->entry);
        pcell->entry = g_strdup (cell->value);
        if (cell->use_bg_color)
          pcell->bg_color = cell->bg_color;
        if (cell->use_fg_color)
          pcell->fg_color = cell->fg_color;
      }
    }

  vcell = gnc_table_get_virtual_cell (table, vcell_loc);
  vcell->user_data = curs->user_data;
}

/* ==================================================== */

void
gnc_table_refresh_header (Table *table)
{
  int cell_row, cell_col;
  VirtualCell *vcell;
  CellBlock *cb;

  if (!table) return;

  vcell = gnc_table_get_header_cell (table);
  if (vcell == NULL) return;

  cb = vcell->cellblock;
  if (cb == NULL) return;

  for (cell_row = 0; cell_row < cb->num_rows; cell_row++)
    for (cell_col = 0; cell_col < cb->num_cols; cell_col++)
    {
      PhysicalLocation ploc = { cell_row, cell_col };
      CellBlockCell *cb_cell;
      PhysicalCell *pcell;

      /* Assumes header starts at physical (0, 0) */
      pcell = gnc_table_get_physical_cell (table, ploc);

      g_free(pcell->entry);

      cb_cell = gnc_cellblock_get_cell(cb, cell_row, cell_col);

      if (cb_cell && cb_cell->cell && cb_cell->cell->value)
        pcell->entry = g_strdup (cb_cell->cell->value);
      else
        pcell->entry = g_strdup ("");
    }
}

/* ==================================================== */

/* gnc_table_verify_cursor_position checks the location of the cursor
 * with respect to a row/column position, and repositions the cursor
 * if necessary. This includes saving any uncommited data in the old
 * cursor, and then moving the cursor and its GUI. Returns true if the
 * cursor was repositioned. */
gboolean
gnc_table_verify_cursor_position (Table *table, PhysicalLocation phys_loc)
{
  gboolean do_commit = FALSE;
  gboolean moved_cursor = FALSE;

  if (!table) return FALSE;

  /* Someone may be trying to intentionally invalidate the cursor, in
   * which case the physical addresses could be out of bounds. For
   * example, in order to unmap it in preparation for a reconfig.
   * So, if the specified location is out of bounds, then the cursor
   * MUST be moved. */
  if (gnc_table_physical_cell_out_of_bounds (table, phys_loc))
    do_commit = TRUE;

  /* Physical position is valid. Check the virtual position. */
  if (!do_commit)
  {
    PhysicalCell *pcell;
    VirtualCellLocation vcell_loc;

    pcell = gnc_table_get_physical_cell (table, phys_loc);

    vcell_loc = pcell->virt_loc.vcell_loc;

    if ((vcell_loc.virt_row != table->current_cursor_virt_loc.virt_row) ||
        (vcell_loc.virt_col != table->current_cursor_virt_loc.virt_col))
      do_commit = TRUE;
  }

  if (do_commit)
  {
    /* before leaving the current virtual position, commit any edits
     * that have been accumulated in the cursor */
    gnc_table_commit_cursor (table);
    gnc_table_move_cursor_gui (table, phys_loc);
    moved_cursor = TRUE;
  }
  else
  {
    /* The request might be to move to a cell that is one column over.
     * If so, then do_commit will be zero, as there will have been no
     * reason to actually move a cursor.  However, we want to keep
     * positions accurate, so record the new location.  (The move may
     * may also be one row up or down, which, for a two-row cursor,
     * also might not require a cursor movement). */
    if (table->current_cursor_phys_loc.phys_row != phys_loc.phys_row)
    {
      table->current_cursor_phys_loc.phys_row = phys_loc.phys_row;
      moved_cursor = TRUE;
    }

    if (table->current_cursor_phys_loc.phys_col != phys_loc.phys_col)
    {
      table->current_cursor_phys_loc.phys_col = phys_loc.phys_col;
      moved_cursor = TRUE;
    }
  }

  return moved_cursor;
}

/* ==================================================== */

void *
gnc_table_get_user_data_physical (Table *table, PhysicalLocation phys_loc)
{
  PhysicalCell *pcell;
  VirtualCell *vcell;

  if (!table) return NULL;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL)
    return NULL;

  vcell = gnc_table_get_virtual_cell (table, pcell->virt_loc.vcell_loc);
  if (vcell == NULL)
    return NULL;

  return vcell->user_data;
}

/* ==================================================== */

void *
gnc_table_get_user_data_virtual (Table *table, VirtualCellLocation vcell_loc)
{
  VirtualCell *vcell;

  if (!table) return NULL;

  vcell = gnc_table_get_virtual_cell (table, vcell_loc);
  if (vcell == NULL)
    return NULL;

  return vcell->user_data;
}

/* ==================================================== */

/* if any of the cells have GUI specific components that need 
 * initialization, initialize them now. The realize() callback
 * on the cursor cell is how we inform the cell handler that 
 * now is the time to initialize its GUI.  */
void
gnc_table_create_cursor (Table * table, CellBlock *curs)
{
  int cell_row, cell_col;

  if (!curs || !table) return;  
  if (!table->ui_data) return;
  
  for (cell_row = 0; cell_row < curs->num_rows; cell_row++)
    for (cell_col = 0; cell_col < curs->num_cols; cell_col++)
    {
      CellBlockCell *cb_cell;

      cb_cell = gnc_cellblock_get_cell (curs, cell_row, cell_col);

      if (cb_cell && cb_cell->cell && cb_cell->cell->realize)
        cb_cell->cell->realize (cb_cell->cell, table->ui_data);
    }
}

/* ==================================================== */

void
gnc_table_wrap_verify_cursor_position (Table *table, PhysicalLocation phys_loc)
{
   CellBlock *save_curs = table->current_cursor;
   PhysicalLocation save_phys_loc;
   gboolean moved_cursor;

   if (!table) return;

   ENTER("(%d %d)", phys_loc.phys_row, phys_loc.phys_col);

   save_phys_loc = table->current_cursor_phys_loc;

   /* VerifyCursor will do all sorts of gui-independent machinations */
   moved_cursor = gnc_table_verify_cursor_position (table, phys_loc);

   if (moved_cursor)
   {
      /* make sure *both* the old and the new cursor rows get redrawn */
      gnc_table_refresh_current_cursor_gui (table, TRUE);
      gnc_table_refresh_cursor_gui (table, save_curs, save_phys_loc, FALSE);
   }

   LEAVE ("\n");
}

/* ==================================================== */

void        
gnc_table_refresh_current_cursor_gui (Table * table, gboolean do_scroll)
{
  if (!table) return;

  gnc_table_refresh_cursor_gui (table, table->current_cursor,
                                table->current_cursor_phys_loc,
                                do_scroll);
}

/* ==================================================== */

gboolean
gnc_table_physical_cell_valid(Table *table,
                              PhysicalLocation phys_loc,
                              gboolean exact_cell)
{
  BasicCell *cell;
  VirtualCell *vcell;
  PhysicalCell *pcell;
  CellBlockCell *cb_cell;
  VirtualLocation virt_loc;

  if (!table) return FALSE;

  pcell = gnc_table_get_physical_cell(table, phys_loc);

  /* can't edit outside of the physical space */
  if (pcell == NULL)
    return FALSE;

  /* header rows cannot be modified */
  vcell = gnc_table_get_header_cell (table);
  if (vcell == NULL)
    return FALSE;

  if (phys_loc.phys_row < vcell->cellblock->num_rows)
    return FALSE;

  /* compute the cell location */
  virt_loc = pcell->virt_loc;

  vcell = gnc_table_get_virtual_cell(table, virt_loc.vcell_loc);
  if (vcell == NULL)
    return FALSE;

  /* verify that offsets are valid. This may occur if the app that is
   * using the table has a paritally initialized cursor. (probably due
   * to a programming error, but maybe they meant to do this). */
  if ((0 > virt_loc.phys_row_offset) || (0 > virt_loc.phys_col_offset))
    return FALSE;

  /* check for a cell handler, but only if cell address is valid */
  if (vcell->cellblock == NULL) return FALSE;

  cb_cell = gnc_cellblock_get_cell (vcell->cellblock,
                                    virt_loc.phys_row_offset,
                                    virt_loc.phys_col_offset);
  if (cb_cell == NULL)
    return FALSE;

  cell = cb_cell->cell;
  if (cell == NULL)
    return FALSE;

  /* if cell is marked as output-only, you can't enter */
  if (0 == (XACC_CELL_ALLOW_INPUT & cell->input_output)) return FALSE;

  /* if cell is pointer only and this is not an exact pointer test,
   * it cannot be entered. */
  if (!exact_cell && ((XACC_CELL_ALLOW_EXACT_ONLY & cell->input_output) != 0))
    return FALSE;

  return TRUE;
}

/* ==================================================== */

/* Handle the non gui-specific parts of a cell enter callback */
const char *
gnc_table_enter_update(Table *table,
                       PhysicalLocation phys_loc,
                       int *cursor_position,
                       int *start_selection,
                       int *end_selection)
{
  const char *retval = NULL;
  PhysicalCell *pcell;
  CellEnterFunc enter;
  CellBlockCell *cb_cell;
  BasicCell *cell;
  CellBlock *cb;
  int cell_row;
  int cell_col;

  if (table == NULL)
    return NULL;

  cb = table->current_cursor;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL)
    return NULL;

  cell_row = pcell->virt_loc.phys_row_offset;
  cell_col = pcell->virt_loc.phys_col_offset;

  ENTER("enter %d %d (relrow=%d relcol=%d) val=%s\n", 
        phys_loc.phys_row, phys_loc.phys_col,
        cell_row, cell_col, pcell->entry);

  /* OK, if there is a callback for this cell, call it */
  cb_cell = gnc_cellblock_get_cell (cb, cell_row, cell_col);
  cell = cb_cell->cell;
  enter = cell->enter_cell;

  if (enter)
  {
    const char *val;

    DEBUG("gnc_table_enter_update(): %d %d has enter handler\n",
          cell_row, cell_col);

    val = pcell->entry;

    retval = enter(cell, val, cursor_position, start_selection, end_selection);

    /* enter() might return null, or it might return a pointer to val,
     * or it might return a new pointer (to newly malloc memory).
     * Replace the old pointer with a new one only if the new one is
     * different, freeing the old one.  (Doing a strcmp would leak
     * memory). */
    if (retval && (val != retval))
    {
      if (safe_strcmp(retval, val) != 0)
        cell->changed = GNC_CELL_CHANGED;
      g_free (pcell->entry);
      pcell->entry = (char *) retval;
    }
    else
      retval = NULL;
  }

  if (table->set_help)
  {
    char *help_str;

    help_str = xaccBasicCellGetHelp(cell);

    table->set_help(table, help_str);

    g_free(help_str);
  }

  LEAVE("return %s\n", retval);

  return retval;
}

/* ==================================================== */

const char *
gnc_table_leave_update(Table *table,
                       PhysicalLocation phys_loc,
                       const char *callback_text)
{
  const char *retval = NULL;
  PhysicalCell *pcell;
  CellLeaveFunc leave;
  CellBlockCell *cb_cell;
  BasicCell *cell;
  CellBlock *cb;
  int cell_row;
  int cell_col;

  if (table == NULL)
    return NULL;

  cb = table->current_cursor;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL)
    return NULL;

  cell_row = pcell->virt_loc.phys_row_offset;
  cell_col = pcell->virt_loc.phys_col_offset;

  ENTER("proposed (%d %d) rel(%d %d) \"%s\"\n",
        phys_loc.phys_row, phys_loc.phys_col,
        cell_row, cell_col, callback_text);

  if (!callback_text)
    callback_text = "";

  /* OK, if there is a callback for this cell, call it */
  cb_cell = gnc_cellblock_get_cell (cb, cell_row, cell_col);
  cell = cb_cell->cell;
  leave = cell->leave_cell;

  if (leave)
  {
    retval = leave(cell, callback_text);

    /* leave() might return null, or it might return a pointer to
     * callback_text, or it might return a new pointer (to newly
     * malloced memory). */
    if (retval == callback_text)
      retval = NULL;
  }

  if (!retval)
    retval = g_strdup (callback_text);

  /* save whatever was returned; but lets check for  
   * changes to avoid roiling the cells too much. */
  if (pcell->entry)
  {
    if (safe_strcmp (pcell->entry, retval))
    {
      g_free (pcell->entry);
      pcell->entry = (char *) retval;
      cell->changed = GNC_CELL_CHANGED;
    }
    else
    {
      /* leave() allocated memory, which we will not be using */
      g_free ((char *) retval);
      retval = NULL;
    }
  }
  else
  {
    pcell->entry = (char *) retval;
    cell->changed = GNC_CELL_CHANGED;
  }

  /* return the result of the final decisionmaking */
  if (safe_strcmp (pcell->entry, callback_text))
    retval = pcell->entry;
  else
    retval = NULL;

  LEAVE("return %s\n", retval);

  return retval;
}

/* ==================================================== */

/* returned result should not be touched by the caller.
 * NULL return value means the edit was rejected. */
const char *
gnc_table_modify_update(Table *table,
                        PhysicalLocation phys_loc,
                        const char *oldval,
                        const char *change,
                        char *newval,
                        int *cursor_position,
                        int *start_selection,
                        int *end_selection)
{
  const char *retval = NULL;
  CellModifyVerifyFunc mv;
  PhysicalCell *pcell;
  CellBlockCell *cb_cell;
  BasicCell *cell;
  CellBlock *cb;
  int cell_row;
  int cell_col;

  if (table == NULL)
    return NULL;

  cb = table->current_cursor;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL)
    return NULL;

  cell_row = pcell->virt_loc.phys_row_offset;
  cell_col = pcell->virt_loc.phys_col_offset;

  ENTER ("\n");

  /* OK, if there is a callback for this cell, call it */
  cb_cell = gnc_cellblock_get_cell (cb, cell_row, cell_col);
  cell = cb_cell->cell;
  mv = cell->modify_verify;

  if (mv)
  {
    retval = mv (cell, oldval, change, newval, cursor_position,
                 start_selection, end_selection);

    /* if the callback returned a non-null value, allow the edit */
    if (retval)
    {
      /* update data. bounds check done earlier */
      g_free (pcell->entry);
      pcell->entry = (char *) retval;
      cell->changed = GNC_CELL_CHANGED;
    }
  }
  else
  {
    /* update data. bounds check done earlier */
    g_free (pcell->entry);
    pcell->entry = newval;
    retval = newval;
    cell->changed = GNC_CELL_CHANGED;
  }

  if (table->set_help)
  {
    char *help_str;

    help_str = xaccBasicCellGetHelp(cell);

    table->set_help(table, help_str);

    g_free(help_str);
  }

  LEAVE ("change %d %d (relrow=%d relcol=%d) val=%s\n", 
         phys_loc.phys_row, phys_loc.phys_col,
         cell_row, cell_col, pcell->entry);

  return retval;
}

/* ==================================================== */

gboolean
gnc_table_direct_update(Table *table,
                        PhysicalLocation phys_loc,
                        const char *oldval,
                        char **newval_ptr,
                        int *cursor_position,
                        int *start_selection,
                        int *end_selection,
                        void *gui_data)
{
  CellBlockCell *cb_cell;
  PhysicalCell *pcell;
  gboolean result;
  BasicCell *cell;
  CellBlock *cb;
  int cell_row;
  int cell_col;

  if (table == NULL)
    return FALSE;

  cb = table->current_cursor;

  pcell = gnc_table_get_physical_cell (table, phys_loc);
  if (pcell == NULL)
    return FALSE;

  cell_row = pcell->virt_loc.phys_row_offset;
  cell_col = pcell->virt_loc.phys_col_offset;

  cb_cell = gnc_cellblock_get_cell (cb, cell_row, cell_col);
  cell = cb_cell->cell;

  ENTER ("\n");

  if (cell->direct_update == NULL)
    return FALSE;

  result = cell->direct_update(cell, oldval, newval_ptr, cursor_position,
                               start_selection, end_selection, gui_data);

  if ((*newval_ptr != oldval) && (*newval_ptr != NULL))
  {
    g_free (pcell->entry);
    pcell->entry = *newval_ptr;
    cell->changed = GNC_CELL_CHANGED;
  }

  if (table->set_help)
  {
    char *help_str;

    help_str = xaccBasicCellGetHelp(cell);

    table->set_help(table, help_str);

    g_free(help_str);
  }

  return result;
}

/* ==================================================== */

gboolean
gnc_table_find_valid_cell_horiz(Table *table,
                                PhysicalLocation *phys_loc,
                                gboolean exact_cell)
{
  int left;
  int right;

  if (phys_loc == NULL)
    return FALSE;

  if (gnc_table_physical_cell_out_of_bounds (table, *phys_loc))
    return FALSE;

  if (gnc_table_physical_cell_valid(table, *phys_loc, exact_cell))
    return TRUE;

  left  = phys_loc->phys_col - 1;
  right = phys_loc->phys_col + 1;

  while (left >= 0 || right < table->num_phys_cols)
  {
    PhysicalLocation ploc;

    ploc.phys_row = phys_loc->phys_row;

    ploc.phys_col = right;
    if (gnc_table_physical_cell_valid(table, ploc, FALSE))
    {
      phys_loc->phys_col = right;
      return TRUE;
    }

    ploc.phys_col = left;
    if (gnc_table_physical_cell_valid(table, ploc, FALSE))
    {
      phys_loc->phys_col = left;
      return TRUE;
    }

    left--;
    right++;
  }

  return FALSE;
}

/* ==================================================== */

gboolean
gnc_table_traverse_update(Table *table,
                          PhysicalLocation phys_loc,
                          gncTableTraversalDir dir,
                          PhysicalLocation *dest_loc)
{
  CellBlock *cb;

  if ((table == NULL) || (dest_loc == NULL))
    return FALSE;

  cb = table->current_cursor;

  ENTER("proposed (%d %d) -> (%d %d)\n",
        phys_loc.phys_row, phys_loc.phys_col,
        dest_loc->phys_row, dest_loc->phys_col);

  /* first, make sure our destination cell is valid. If it is out
   * of bounds report an error. I don't think this ever happens. */
  if (gnc_table_physical_cell_out_of_bounds (table, *dest_loc))
  {
    PERR("destination (%d, %d) out of bounds (%d, %d)\n",
         dest_loc->phys_row, dest_loc->phys_col,
         table->num_phys_rows, table->num_phys_cols);
    return TRUE;
  }

  /* next, check the current row and column.  If they are out of bounds
   * we can recover by treating the traversal as a mouse point. This can
   * occur whenever the register widget is resized smaller, maybe?. */
  if (gnc_table_physical_cell_out_of_bounds (table, phys_loc))
  {
    PINFO("source (%d, %d) out of bounds (%d, %d)\n",
	  phys_loc.phys_row, phys_loc.phys_col,
          table->num_phys_rows, table->num_phys_cols);

    dir = GNC_TABLE_TRAVERSE_POINTER;
  }

  /* process forward-moving traversals */
  switch (dir)
  {
    case GNC_TABLE_TRAVERSE_RIGHT:
    case GNC_TABLE_TRAVERSE_LEFT:      
      {
        CellTraverseInfo *ct_info;
        PhysicalCell *pcell;
        int cell_row, cell_col;

	/* cannot compute the cell location until we have checked that
	 * row and column have valid values. compute the cell
	 * location. */
        pcell = gnc_table_get_physical_cell (table, phys_loc);
        if (pcell == NULL)
          return FALSE;

        cell_row = pcell->virt_loc.phys_row_offset;
        cell_col = pcell->virt_loc.phys_col_offset;

        ct_info = gnc_cellblock_get_traverse (cb, cell_row, cell_col);

        if (dir == GNC_TABLE_TRAVERSE_RIGHT)
        {
          dest_loc->phys_row = (phys_loc.phys_row - cell_row +
                                ct_info->right_traverse_row);
          dest_loc->phys_col = (phys_loc.phys_col - cell_col +
                                ct_info->right_traverse_col);
        }
        else
        {
          dest_loc->phys_row = (phys_loc.phys_row - cell_row +
                                ct_info->left_traverse_row);
          dest_loc->phys_col = (phys_loc.phys_col - cell_col +
                                ct_info->left_traverse_col);
        }
      }

      break;

    case GNC_TABLE_TRAVERSE_UP:
    case GNC_TABLE_TRAVERSE_DOWN:
      {
        VirtualCell *vcell = gnc_table_get_header_cell (table);
	CellBlock *header = vcell->cellblock;
	PhysicalLocation new_loc = *dest_loc;
	int increment;

	/* Keep going in the specified direction until we find a valid
	 * row to land on, or we hit the end of the table. At the end,
	 * turn around and go back until we find a valid row or we get
	 * to where we started. If we still can't find anything, try
	 * going left and right. */
	increment = (dir == GNC_TABLE_TRAVERSE_DOWN) ? 1 : -1;

	while (!gnc_table_physical_cell_valid(table, new_loc, FALSE))
	{
	  if (new_loc.phys_row == phys_loc.phys_row)
          {
            new_loc.phys_row = dest_loc->phys_row;
            gnc_table_find_valid_cell_horiz(table, &new_loc, FALSE);
            break;
          }

	  if ((new_loc.phys_row < header->num_rows) ||
              (new_loc.phys_row >= table->num_phys_rows))
	  {
	    increment *= -1;
	    new_loc.phys_row = dest_loc->phys_row;
	  }

	  new_loc.phys_row += increment;
	}

	*dest_loc = new_loc;
      }

      if (!gnc_table_physical_cell_valid(table, *dest_loc, FALSE))
	return TRUE;

      break;

    case GNC_TABLE_TRAVERSE_POINTER:
      if (!gnc_table_find_valid_cell_horiz(table, dest_loc, TRUE))
        return TRUE;

      break;

    default:
      /* shouldn't be reached */
      assert(0);
      break;
  }

  /* Call the table traverse callback for any modifications. */
  if (table->traverse)
    (table->traverse) (table, dest_loc, dir);

  LEAVE("dest_row = %d, dest_col = %d\n",
        dest_loc->phys_row, dest_loc->phys_col);

  return FALSE;
}

/* ================== end of file ======================= */