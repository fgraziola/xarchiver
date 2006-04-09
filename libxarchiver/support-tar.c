/*
 *  Copyright (c) 2006 Stephan Arts      <stephan.arts@hva.nl>
 *                     Giuseppe Torelli <colossus73@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Library General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* TODO:
 * Currently this implementation only checks for USTAR magic-header
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include "internals.h"
#include "libxarchiver.h"
#include "support-tar.h"

/*
 * xarchive_tar_support_remove(XArchive *archive, GSList *files)
 * Remove files and folders from archive
 */
gboolean
xarchive_tar_support_remove (XArchive *archive, GSList *files)
{
	gchar *command;
	GString *names;
	
	GSList *_files = files;
	if(files != NULL)
	{
		names = concatenatefilenames ( _files );
		command = g_strconcat ( "tar --delete -vf " , archive->path , names->str , NULL );
		archive->child_pid = xarchiver_async_process ( archive , command, 0);
		archive->status = REMOVE;
		//TODO: to reload the archive to show the changes in the liststore -- GUI thing
		g_free(command);
		g_string_free (names, TRUE);
	}
	fchdir(n_cwd);
	return TRUE;
}

/*
 * xarchive_tar_support_add(XArchive *archive, GSList *files)
 * Add files and folders to archive
 */
gboolean
xarchive_tar_support_add (XArchive *archive, GSList *files)
{
	gchar *command, *dir;
	GString *names;
	
	GSList *_files = files;
	if(files != NULL)
	{
		dir = g_path_get_dirname(_files->data);
		chdir(dir);
		g_free(dir);

		names = concatenatefilenames ( _files );		
		// Check if the archive already exists or not
		if(g_file_test(archive->path, G_FILE_TEST_EXISTS))
			command = g_strconcat("tar rvvf ", archive->path, " ", names->str, NULL);
		else
			command = g_strconcat("tar cvvf ", archive->path, " ", names->str, NULL);

		archive->status = ADD;
		archive->child_pid = xarchiver_async_process ( archive , command, 0);
		g_free(command);
		if (archive->child_pid == 0)
		{
			g_message (archive->error->message);
			g_error_free (archive->error);
			return FALSE;
		}
		g_string_free(names, TRUE);
	}
	fchdir(n_cwd);
	return TRUE;
}

/*
 * xarchive_tar_support_extract(XArchive *archive, GSList *files)
 * Extract files and folders from archive
 */
gboolean
xarchive_tar_support_extract(XArchive *archive, gchar *destination_path, GSList *files, gboolean full_path)
{
	gchar *command, *dir, *filename;
	unsigned short int levels;
	char digit[2];
	gchar *strip = NULL;
    
	if(!g_file_test(archive->path, G_FILE_TEST_EXISTS))
		return FALSE;
    
	// Only extract certain files
	if( (files == NULL) && (g_slist_length(files) == 0))
	{
		dir = g_path_get_dirname(files->data);
		chdir(dir);
		g_free(dir);
		filename = g_path_get_basename(files->data);
		command = g_strconcat("tar xvvf ", archive->path, " -C ", destination_path, " ", filename, NULL);
	} 
	else
	{
		GSList *_files = files;
		GString *names;
		names = concatenatefilenames ( _files );
		if ( full_path == 0 )
		{
			levels = countcharacters ( names->str , '/');
			sprintf ( digit , "%d" , levels );
			strip = g_strconcat ( "--strip-components=" , digit , " " , NULL );
		}
		command = g_strconcat("tar " , full_path ? "" : strip , "-xvf ", archive->path, " -C ", destination_path, names->str , NULL);
		g_string_free (names,TRUE);
	}
	archive->child_pid = xarchiver_async_process ( archive , command,0);
	g_free(command);
	if ( strip != NULL)
		g_free ( strip );
	if (archive->child_pid == 0)
	{
		g_message (archive->error->message);
		g_error_free (archive->error);
		return FALSE;
	}
	if ( ! xarchiver_set_channel ( archive->output_fd, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, xarchiver_output_function, NULL ) ) return FALSE;
	if (! xarchiver_set_channel ( archive->error_fd, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, xarchiver_error_function, NULL ) ) return FALSE;
	fchdir(n_cwd);
	return TRUE;
}

gboolean
xarchive_tar_support_verify(XArchive *archive)
{
	FILE *fp;
	unsigned char magic[5];

	if( (archive->path) && (archive->type == XARCHIVETYPE_UNKNOWN))
	{
		fp = fopen(archive->path, "r");
		if(fp == 0)
			return FALSE;
		fseek ( fp, 0 , SEEK_SET );
		if ( fseek ( fp , 257, SEEK_CUR ) == 0 ) 
		{
			if ( fread ( magic, 1, 5, fp ) )
			{
				if ( memcmp ( magic,"ustar",5 ) == 0 )
				{
					archive->type = XARCHIVETYPE_TAR;
					archive->has_passwd = 0;
					archive->passwd = 0;
				}
			}
		}
		fclose( fp );
	}

	if(archive->type == XARCHIVETYPE_TAR)
		return TRUE;
	else
		return FALSE;
}

/*
 * xarchive_tar_support_open(XArchive *archive)
 * Open the archive and calls other functions to catch the output
 *
 */

gboolean
xarchive_tar_support_open (XArchive *archive)
{
	gchar *command;

	if(archive->row)
	{
		g_list_free(archive->row);
	}
	command = g_strconcat ( "tar tfv " , archive->path, NULL );
	archive->child_pid = xarchiver_async_process ( archive , command , 0 );
	g_free (command);
	if (archive->child_pid == 0)
	{
		g_message (archive->error->message);
		g_error_free (archive->error);
	}
	if ( ! xarchiver_set_channel ( archive->output_fd, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, xarchiver_parse_tar_output, archive ) )
		return FALSE;
	if (! xarchiver_set_channel ( archive->error_fd, G_IO_IN|G_IO_PRI|G_IO_ERR|G_IO_HUP|G_IO_NVAL, xarchiver_error_function, archive ) )
		return FALSE;
	archive->dummy_size = 0;
	return TRUE;
}

/*
 * xarchive_parse_tar_output
 * Parse the output from the rar command when opening the archive
 *
 */

gboolean xarchiver_parse_tar_output (GIOChannel *ioc, GIOCondition cond, gpointer data)
{
	gchar *line = NULL;
	XArchive *archive = data;

	if (cond & (G_IO_IN | G_IO_PRI) )
	{
		g_io_channel_read_line ( ioc, &line, NULL, NULL, NULL );
		if (line != NULL && ! archive->status == RELOAD )
			archive->output = g_slist_prepend ( archive->output , line );
		archive->row = get_last_field ( archive->row , line , 6 );
		archive->row = split_line (archive->row , line , 5);
		if ( strstr ((gchar *)g_list_nth_data ( archive->row , 1) , "d") == NULL )
			archive->number_of_files++;
		else
			archive->number_of_dirs++;
		archive->dummy_size += atoll ( (gchar*)g_list_nth_data ( archive->row,2) );
		return TRUE;
	}
	else if (cond & (G_IO_ERR | G_IO_HUP | G_IO_NVAL) )
	{
		g_io_channel_shutdown ( ioc,TRUE,NULL );
		g_io_channel_unref (ioc);
		return FALSE;
	}
	return TRUE;
}

XArchiveSupport *
xarchive_tar_support_new()
{
	XArchiveSupport *support = g_new0(XArchiveSupport, 1);
	support->type    = XARCHIVETYPE_TAR;
	support->add     = xarchive_tar_support_add;
	support->verify  = xarchive_tar_support_verify;
	support->extract = xarchive_tar_support_extract;
	support->remove  = xarchive_tar_support_remove;
	support->open    = xarchive_tar_support_open;
	support->n_columns = 6;
	support->column_names  = g_new0(gchar *, support->n_columns);
	support->column_types  = g_new0(GType, support->n_columns);
	support->column_names[0] = "Filename";
	support->column_names[1] = "Permissions";
	support->column_names[2] = "Owner / Group";
	support->column_names[3] = "Size";
	support->column_names[4] = "Date";
	support->column_names[5] = "Time";
	support->column_types[0] = G_TYPE_STRING;
	support->column_types[1] = G_TYPE_STRING;
	support->column_types[2] = G_TYPE_STRING;
	support->column_types[3] = G_TYPE_STRING;
	support->column_types[4] = G_TYPE_STRING;
	support->column_types[5] = G_TYPE_STRING;
	
	return support;
}
