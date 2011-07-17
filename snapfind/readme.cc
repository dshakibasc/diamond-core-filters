/*
 *  SnapFind
 *  An interactive image search application
 *  Version 1
 *
 *  Copyright (c) 2009 Carnegie Mellon University
 *  All Rights Reserved.
 *
 *  This software is distributed under the terms of the Eclipse Public
 *  License, Version 1.0 which can be found in the file named LICENSE.
 *  ANY USE, REPRODUCTION OR DISTRIBUTION OF THIS SOFTWARE CONSTITUTES
 *  RECIPIENT'S ACCEPTANCE OF THIS AGREEMENT
 */


#include "readme.h"

void
print_usage() {
  printf(
"SnapFind plugin-runner help\n"
"===========================\n"
"The SnapFind plugin-runner allows you to list, edit, and locally run legacy\n"
"SnapFind plugins in a programmatic way. The output of the plugin-runner\n"
"provides enough information to allow the submission of searches to an\n"
"OpenDiamond system.\n"
"\n"
"\n"
"Commands\n"
"--------\n"
" * list-plugins\n"
"\n"
"   produce a newline-delimited K-V list of plugins\n"
"\n"
"   useful output properties:\n"
"     type: \"filter\" or \"codec\"\n"
"\n"
"     display-name: string representing the user-facing name\n"
"\n"
"     internal-name: identifying string used in subsequent commands\n"
"\n"
"     needs-patches: \"true\" or \"false\" depending on if the plugin needs patches\n"
"                    to be useful (example-based)\n"
"\n"
"\n"
" * get-plugin-initial-config <type> <internal-name>\n"
"\n"
"   for a given type and internal-name, output a plugin's default parameters\n"
"   and other properties in K-V format on stdout\n"
"\n"
"   useful output properties:\n"
"     is-editable: \"true\" or \"false\" depending on if the plugin can be edited\n"
"\n"
"     fspec: fspec for submitting to an OpenDiamond system. You must replace the\n"
"            character '*' with a valid and unique filter name before submitting\n"
"            to an OpenDiamond system. It is recommended not to use all caps for\n"
"            this value. You must also replace the character '@' with the MD5\n"
"            hash of the filter code associated with this plugin.\n"
"\n"
"     blob: blob for submitting to an OpenDiamond system\n"
"\n"
"     name: the user-specified name of the filter, or a default name\n"
"\n"
"     patch-count: number of patches (given in properties prefixed by \"patch-\")\n"
"\n"
"     patch-%%d: a patch in PPM format, where \"%%d\" is an integer starting from 0\n"
"\n"
"     searchlet-lib-path: fully-qualified path to the searchlet library for\n"
"       submitting to an OpenDiamond system\n"
"\n"
"     config: internal (opaque) serialization of the user interface\n"
"\n"
"\n"
" * edit-plugin-config <type> <internal-name>\n"
"   for a given type and internal-name, take K-V input on stdin (including the\n"
"   opaque \"config\" property), display a window asking the user to modify plugin\n"
"   parameters, and output new properties in K-V format on stdout\n"
"\n"
"   useful input properties:\n"
"     config: internal (opaque) serialization of the user interface\n"
"\n"
"     patch-count: number of patches (given in properties prefixed by \"patch-\")\n"
"\n"
"     patch-%%d: a patch in PPM format, where \"%%d\" is an integer starting from 0\n"
"\n"
"     name: the name of the filter to display to the user\n"
"\n"
"   useful output properties:\n"
"     same as for \"get-plugin-initial-config\"\n"
"\n"
"\n"
" * normalize-plugin-config <type> <internal-name>\n"
"   for a given type and internal-name, take K-V input on stdin (including the\n"
"   opaque \"config\" property), internally normalize the config, and output new\n"
"   properties in K-V format on stdout (useful for processing new patches)\n"
"\n"
"   useful input properties:\n"
"     same as for \"edit-plugin-config\"\n"
"\n"
"   useful output properties:\n"
"     same as for \"edit-plugin-config\"\n"
"\n"
"\n"
"K-V format\n"
"----------\n"
"The K-V format is a representation of key-value pairs, designed to be easily\n"
"read and written, with direct support for binary data.\n"
"\n"
"A single key-value pair is represented by:\n"
"\n"
"K (length of key in ASCII decimal)(newline)\n"
"(key)(newline)\n"
"V (length of value in ASCII decimal)(newline)\n"
"(value)(newline)\n"
"\n"
"\n"
"Multiple key-value pairs are concatenated together without intervening space.\n"
"\n"
"A \"newline-delimited K-V list\" is a sequence of sets of key-value pairs,\n"
"separated by newlines.\n"
);
}
