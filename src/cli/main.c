/* -*- mode: c; c-file-style: "gnu" -*-
 * Copyright (C) 2013 Cryptotronix, LLC.
 *
 * This file is part of EClet.
 *
 * EClet is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * EClet is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with EClet.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * @file   main.c
 * @author Joshua Datko <jbd@cryptotronix.com>
 * @date   Sat May 24 20:47:39 2014
 *
 * @brief  Main entry point for EClet command line driver
 *
 *
 */

#include <argp.h>
#include <assert.h>
#include "cli_commands.h"
#include "config.h"
#include <string.h>
#include <libcryptoauth.h>

const char *argp_program_version = PACKAGE_VERSION;

const char *argp_program_bug_address = PACKAGE_BUGREPORT;

/* Program documentation. */
static char doc[] =
  "EClet is a program to interface to the Cryptotronix EClet which contains\n"
  "an Atmel ATECC108\n\n"
  "Currently implemented Commands:\n\n"
  "personalize   --  You should run this command first upon receiving your\n"
  "                  EClet.\n"
  "random        --  Retrieves 32 bytes of random data from the device.\n"
  "serial-num    --  Retrieves the device's serial number.\n"
  "get-config    --  Dumps the configuration zone\n"
  "get-otp       --  Dumps the OTP (one time programmable) zone\n"
  "state         --  Returns the device's state.\n"
  "                  Factory -- Random will produced a fixed 0xFFFF0000\n"
  "                  Initialized -- Configuration is locked, keys may be \n"
  "                                 written\n"
  "                  Personalized -- Keys are loaded.  Memory is locked\n"
  "gen-key       --  Generates a P256 Private key in the specified key slot\n"
  "                  Returns the Public Key (x,y) with the leading uncompressed\n"
  "                  point format tag (0x04)\n"
  "get-pub       --  returns the public key. get a public key from a specific\n"
  "                  slot use 'get-pub -k <slot>' \n"
  "sign          --  Performs an ECDSA signature using the NIST P-256 curve.\n"
  "                  Specify the file to signed with -f, which will be SHA-256\n"
  "                  hashed prior to signing. Specify the key with -k.\n"
  "                  Returns the signature (R,S)\n"
  "verify        --  Uses the device to verify the signature.\n"
  "                  Specify the public key with --public-key, you must include\n"
  "                    the 0x04 tag followed by xy\n"
  "                  Specify the signature with --signature\n"
  "                  Specify the file with -f, it will be hashed with SHA256\n"
  "offline-verify-sign\n"
  "              --  Same as verify except it does NOT use the device, but a \n"
  "                  software library.";


/* A description of the arguments we accept. */
static char args_doc[] = "command";

#define OPT_UPDATE_SEED 300
#define OPT_SIGNATURE 301
#define OPT_PUB_KEY 302

/* The options we understand. */
static struct argp_option options[] = {
  { 0, 0, 0, 0, "Global Options:", -1},
  {"verbose",  'v', 0,      0,  "Produce verbose output" },
  {"quiet",    'q', 0,      0,  "Don't produce any output" },
  {"silent",   's', 0,      OPTION_ALIAS },
  {"bus",      'b', "BUS",  0,  "I2C bus: defaults to /dev/i2c-1"},
  {"address",  'a', "ADDRESS",      0,  "i2c address for the device (in hex)"},
  {"file",     'f', "FILE",         0,  "Read from FILE vs. stdin"},
  { 0, 0, 0, 0, "Sign and Verify Operations:", 1},
  {"signature", OPT_SIGNATURE, "SIGNATURE", 0, "The signature to be verified"},
  {"public-key", OPT_PUB_KEY, "PUBLIC_KEY", 0,
   "The public key that produced the signature"},
  { 0, 0, 0, 0, "Random Command Options:", 2},
  {"update-seed", OPT_UPDATE_SEED, 0, 0,
     "Updates the random seed.  Only applicable to certain commands"},
  { 0, 0, 0, 0, "Key related command options:", 3},
  {"key-slot", 'k', "SLOT",      0,  "The internal key slot to use."},
  {"write", 'w', "WRITE",      0,
   "The 32 byte data to write to a slot (64 bytes of ASCII Hex)"},
  { 0, 0, 0, 0, "Check and Offline-Verify Mac Options:", 4},
  {"challenge", 'c', "CHALLENGE",      0,
   "The 32 byte challenge (64 bytes of ASCII Hex)"},
  {"challenge-response", 'r', "CHALLENGE_RESPONSE",      0,
   "The 32 byte challenge response (64 bytes of ASCII Hex)"},
  {"meta-data", 'm', "META",      0,
   "The 13 byte meta data associated with the mac (26 bytes of ASCII Hex)"},
  { 0 }
};


/* Parse a single option. */
static error_t
parse_opt (int key, char *arg, struct argp_state *state)
{
  /* Get the input argument from argp_parse, which we
     know is a pointer to our arguments structure. */
  struct arguments *arguments = state->input;
  int slot;
  long int address_arg;

  switch (key)
    {
    case 'a':

      /* TODO: Not working as expected */
      address_arg = strtol (arg,NULL,16);
      if (0 != address_arg)
        {
          arguments->address = address_arg;
          LCA_LOG (DEBUG, "Using address %u", address_arg);
        }
      else
        LCA_LOG (INFO, "Address not recognized, using default");

      break;
    case 'b':
      arguments->bus = arg;
      break;
    case 'q': case 's':
      arguments->silent = 1;
      break;
    case 'v':
      arguments->verbose = 1;
      lca_set_log_level (DEBUG);
      break;
    case 'f':
      arguments->input_file = arg;
      break;
    case OPT_UPDATE_SEED:
      arguments->update_seed = true;
      break;
    case 'k':
      slot = atoi (arg);
      if (slot < 0 || slot > 15)
        argp_usage (state);

      arguments->key_slot = slot;
      break;
    case 'c':
      if (!is_hex_arg (arg, 64))
        {
          fprintf (stderr, "%s\n", "Invalid Challenge.");
          argp_usage (state);
        }
      else
        arguments->challenge = arg;
      break;
    case OPT_SIGNATURE:
      if (!is_hex_arg (arg, 128))
        {
          fprintf (stderr, "%s\n", "Invalid P256 Signature.");
          argp_usage (state);
        }
      else
      arguments->signature = arg;
      break;
    case OPT_PUB_KEY:
      if (!is_hex_arg (arg, 130))
        {
          fprintf (stderr, "%s\n", "Invalid P256 Public Key.");
          argp_usage (state);
        }
      else
        arguments->pub_key = arg;
      break;
    case 'w':
      if (!is_hex_arg (arg, 64))
        {
          fprintf (stderr, "%s\n", "Invalid Data.");
          argp_usage (state);
        }
      else
        arguments->write_data = arg;
      break;
    case 'r':
      if (!is_hex_arg (arg, 64))
        {
          fprintf (stderr, "%s\n", "Invalid Challenge Response.");
          argp_usage (state);
        }
      else
        arguments->challenge_rsp = arg;
      break;
    case 'm':
      if (!is_hex_arg (arg, 26))
        {
          fprintf (stderr, "%s\n", "Invalid Meta Data.");
          argp_usage (state);
        }
      else
        arguments->meta = arg;
      break;
    case ARGP_KEY_ARG:
      if (state->arg_num >= NUM_ARGS)
        /* Too many arguments. */
        argp_usage (state);
      else
        arguments->args[state->arg_num] = arg;

      break;

    case ARGP_KEY_END:
      if (state->arg_num < NUM_ARGS)
        /* Not enough arguments. */
        argp_usage (state);
      break;

    default:
      return ARGP_ERR_UNKNOWN;
    }
  return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

int main (int argc, char **argv)
{
  struct arguments arguments;

  /* Sets arguments defaults and the command list */
  init_cli (&arguments);

  /* Parse our arguments; every option seen by parse_opt will
     be reflected in arguments. */
  argp_parse (&argp, argc, argv, 0, 0, &arguments);

  exit (dispatch (arguments.args[0], &arguments));

}
