#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include "generated-code/test.pb-c.h"
#include <google/protobuf-c/protobuf-c-rpc.h>

static unsigned database_size;
static Foo_Person_pbc *database;		/* sorted by name */

static void
die (const char *format, ...)
{
  va_list args;
  va_start (args, format);
  vfprintf (stderr, format, args);
  va_end (args);
  fprintf (stderr, "\n");
  exit (1);
}

static void
usage (void)
{
  die ("usage: example-server [--port=NUM | --unix=PATH] --database=INPUT\n"
       "\n"
       "Run a protobuf server as specified by the DirLookup service\n"
       "in the test.proto file in the protobuf-c distribution.\n"
       "\n"
       "Options:\n"
       "  --port=NUM       Port to listen on for RPC clients.\n"
       "  --unix=PATH      Unix-domain socket to listen on.\n"
       "  --database=FILE  data which the server will use to answer requests.\n"
       "\n"
       "The database file is a sequence of stanzas, one per person:\n"
       "\n"
       "dave\n"
       " email who@cares.com\n"
       " mobile (123)123-1234\n"
       " id 666\n"
       "\n"
       "notes:\n"
       "- each stanza begins with a single unindented line, the person's name.\n");
}

static void *xmalloc (size_t size)
{
  void *rv;
  if (size == 0)
    return NULL;
  rv = malloc (size);
  if (rv == NULL)
    die ("out-of-memory allocating %u bytes", (unsigned) size);
  return rv;
}

static void *xrealloc (void *a, size_t size)
{
  void *rv;
  if (size == 0)
    {
      free (a);
      return NULL;
    }
  if (a == NULL)
    return xmalloc (size);
  rv = realloc (a, size);
  if (rv == NULL)
    die ("out-of-memory re-allocating %u bytes", (unsigned) size);
  return rv;
}

static char *xstrdup (const char *str)
{
  if (str == NULL)
    return NULL;
  return strcpy (xmalloc (strlen (str) + 1), str);
}

static char *peek_next_token (char *buf)
{
  while (*buf && !isspace (*buf))
    buf++;
  while (*buf && isspace (*buf))
    buf++;
  return buf;
}

static protobuf_c_boolean is_whitespace (const char *text)
{
  while (*text != 0)
    {
      if (!isspace (*text))
        return 0;
      text++;
    }
  return 1;
}
static void chomp_trailing_whitespace (char *buf)
{
  unsigned len = strlen (buf);
  while (len > 0)
    {
      if (!isspace (buf[len-1]))
        break;
      len--;
    }
  buf[len] = 0;
}
static protobuf_c_boolean starts_with (const char *str, const char *prefix)
{
  return memcmp (str, prefix, strlen (prefix)) == 0;
}

static int
compare_persons_by_name (const void *a, const void *b)
{
  return strcmp (((const Foo_Person_pbc*)a)->name, ((const Foo_Person_pbc*)b)->name);
}
static void
load_database (const char *filename)
{
  FILE *fp = fopen (filename, "r");
  char buf[2048];
  unsigned n_people = 0;
  unsigned people_alloced = 32;
  unsigned line_no;
  Foo_Person_pbc *people = xmalloc (sizeof (Foo_Person_pbc) * people_alloced);
  if (fp == NULL)
    die ("error opening %s: %s", filename, strerror (errno));
  line_no = 0;
  while (fgets (buf, sizeof (buf), fp) != NULL)
    {
      line_no++;
      if (buf[0] == '#')
	continue;
      if (is_whitespace (buf))
        continue;
      chomp_trailing_whitespace (buf);
      if (isspace (buf[0]))
        {
	  Foo_Person_pbc *person;
          char *start = buf + 1;
	  if (n_people == 0)
	    die ("error on %s, line %u: line began with a space, but no person's name preceded it",
	         filename, line_no);
          person = people + (n_people - 1);
          while (*start && isspace (*start))
            start++;
          if (starts_with (start, "id "))
            person->id = atoi (peek_next_token (start));
          else if (starts_with (start, "email "))
            person->email = xstrdup (peek_next_token (start));
          else if (starts_with (start, "mobile ")
              ||   starts_with (start, "home ")
              ||   starts_with (start, "work "))
            {
              Foo_Person_PhoneNumber_pbc *pn = xmalloc (sizeof (Foo_Person_PhoneNumber_pbc));
              Foo_Person_PhoneNumber_pbc tmp = FOO_PERSON_PHONE_NUMBER_PBC_INIT;
              tmp.has_type = 1;
              tmp.type = start[0] == 'm' ? FOO_PERSON_PHONE_TYPE_MOBILE
                       : start[0] == 'h' ? FOO_PERSON_PHONE_TYPE_HOME
                       : FOO_PERSON_PHONE_TYPE_WORK;
              tmp.number = xstrdup (peek_next_token (start));
              person->phone = xrealloc (person->phone, sizeof (Foo_Person_PhoneNumber_pbc*) * (person->n_phone+1));
              *pn = tmp;
              person->phone[person->n_phone++] = pn;
            }
          else
            die ("error on %s, line %u: unrecognized field starting with %s", filename, line_no, start);
	}
      else
        {
	  Foo_Person_pbc *person;
	  if (n_people == people_alloced)
	    {
              people_alloced *= 2;
              people = xrealloc (people, people_alloced * sizeof (Foo_Person_pbc));
	    }
	  person = people + n_people++;
	  foo_person_init (person);
	  person->name = xstrdup (buf);
	}
    }
  if (n_people == 0)
    die ("empty database: insufficiently interesting to procede");
  
  qsort (people, n_people, sizeof (Foo_Person_pbc), compare_persons_by_name);

  database = people;
  database_size = n_people;
  fclose (fp);
}

static int
compare_name_to_person (const void *a, const void *b)
{
  return strcmp (a, ((const Foo_Person_pbc*)b)->name);
}
static void
example_by_name (Foo_DirLookup_Service    *service,
                 const Foo_Name_pbc           *name,
                 Foo_LookupResult_pbc_Closure  closure,
                 void                      *closure_data)
{
  (void) service;
  if (name == NULL || name->name == NULL)
    closure (NULL, closure_data);
  else
    {
      Foo_LookupResult_pbc result = FOO_LOOKUP_RESULT_PBC_INIT;
      Foo_Person_pbc *rv = bsearch (name->name, database, database_size,
                                 sizeof (Foo_Person_pbc), compare_name_to_person);
      if (rv != NULL)
        result.person = rv;
      closure (&result, closure_data);
    }
}

static Foo_DirLookup_Service the_dir_lookup_service =
  FOO_DIR_LOOKUP_INIT(example_);

int main(int argc, char**argv)
{
  ProtobufC_RPC_Server *server;
  ProtobufC_RPC_AddressType address_type=0;
  const char *name = NULL;
  unsigned i;

  for (i = 1; i < (unsigned) argc; i++)
    {
      if (starts_with (argv[i], "--port="))
        {
          address_type = PROTOBUF_C_RPC_ADDRESS_TCP;
          name = strchr (argv[i], '=') + 1;
        }
      else if (starts_with (argv[i], "--unix="))
        {
          address_type = PROTOBUF_C_RPC_ADDRESS_LOCAL;
          name = strchr (argv[i], '=') + 1;
        }
      else if (starts_with (argv[i], "--database="))
        {
          load_database (strchr (argv[i], '=') + 1);
        }
      else
        usage ();
    }

  if (database_size == 0)
    die ("missing --database=FILE (try --database=example.database)");
  if (name == NULL)
    die ("missing --port=NUM or --unix=PATH");
  
  signal (SIGPIPE, SIG_IGN);

  server = protobuf_c_rpc_server_new (address_type, name, (ProtobufCService *) &the_dir_lookup_service, NULL);

  for (;;)
    protobuf_c_dispatch_run (protobuf_c_dispatch_default ());
  return 0;
}
