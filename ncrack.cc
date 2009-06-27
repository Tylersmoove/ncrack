
/***************************************************************************
 * ncrack.cc -- ncrack's core engine along with all nsock callback         *
 * handlers reside in here. Simple options' (not host or service-options   *
 * specification handling) parsing also happens in main() here.            *
 *                                                                         *
 ***********************IMPORTANT NMAP LICENSE TERMS************************
 *                                                                         *
 * The Nmap Security Scanner is (C) 1996-2009 Insecure.Com LLC. Nmap is    *
 * also a registered trademark of Insecure.Com LLC.  This program is free  *
 * software; you may redistribute and/or modify it under the terms of the  *
 * GNU General Public License as published by the Free Software            *
 * Foundation; Version 2 with the clarifications and exceptions described  *
 * below.  This guarantees your right to use, modify, and redistribute     *
 * this software under certain conditions.  If you wish to embed Nmap      *
 * technology into proprietary software, we sell alternative licenses      *
 * (contact sales@insecure.com).  Dozens of software vendors already       *
 * license Nmap technology such as host discovery, port scanning, OS       *
 * detection, and version detection.                                       *
 *                                                                         *
 * Note that the GPL places important restrictions on "derived works", yet *
 * it does not provide a detailed definition of that term.  To avoid       *
 * misunderstandings, we consider an application to constitute a           *
 * "derivative work" for the purpose of this license if it does any of the *
 * following:                                                              *
 * o Integrates source code from Nmap                                      *
 * o Reads or includes Nmap copyrighted data files, such as                *
 *   nmap-os-db or nmap-service-probes.                                    *
 * o Executes Nmap and parses the results (as opposed to typical shell or  *
 *   execution-menu apps, which simply display raw Nmap output and so are  *
 *   not derivative works.)                                                * 
 * o Integrates/includes/aggregates Nmap into a proprietary executable     *
 *   installer, such as those produced by InstallShield.                   *
 * o Links to a library or executes a program that does any of the above   *
 *                                                                         *
 * The term "Nmap" should be taken to also include any portions or derived *
 * works of Nmap.  This list is not exclusive, but is meant to clarify our *
 * interpretation of derived works with some common examples.  Our         *
 * interpretation applies only to Nmap--we don't speak for other people's  *
 * GPL works.                                                              *
 *                                                                         *
 * If you have any questions about the GPL licensing restrictions on using *
 * Nmap in non-GPL works, we would be happy to help.  As mentioned above,  *
 * we also offer alternative license to integrate Nmap into proprietary    *
 * applications and appliances.  These contracts have been sold to dozens  *
 * of software vendors, and generally include a perpetual license as well  *
 * as providing for priority support and updates as well as helping to     *
 * fund the continued development of Nmap technology.  Please email        *
 * sales@insecure.com for further information.                             *
 *                                                                         *
 * As a special exception to the GPL terms, Insecure.Com LLC grants        *
 * permission to link the code of this program with any version of the     *
 * OpenSSL library which is distributed under a license identical to that  *
 * listed in the included COPYING.OpenSSL file, and distribute linked      *
 * combinations including the two. You must obey the GNU GPL in all        *
 * respects for all of the code used other than OpenSSL.  If you modify    *
 * this file, you may extend this exception to your version of the file,   *
 * but you are not obligated to do so.                                     *
 *                                                                         *
 * If you received these files with a written license agreement or         *
 * contract stating terms other than the terms above, then that            *
 * alternative license agreement takes precedence over these comments.     *
 *                                                                         *
 * Source is provided to this software because we believe users have a     *
 * right to know exactly what a program is going to do before they run it. *
 * This also allows you to audit the software for security holes (none     *
 * have been found so far).                                                *
 *                                                                         *
 * Source code also allows you to port Nmap to new platforms, fix bugs,    *
 * and add new features.  You are highly encouraged to send your changes   *
 * to nmap-dev@insecure.org for possible incorporation into the main       *
 * distribution.  By sending these changes to Fyodor or one of the         *
 * Insecure.Org development mailing lists, it is assumed that you are      *
 * offering the Nmap Project (Insecure.Com LLC) the unlimited,             *
 * non-exclusive right to reuse, modify, and relicense the code.  Nmap     *
 * will always be available Open Source, but this is important because the *
 * inability to relicense code has caused devastating problems for other   *
 * Free Software projects (such as KDE and NASM).  We also occasionally    *
 * relicense the code to third parties as discussed above.  If you wish to *
 * specify special license conditions of your contributions, just say so   *
 * when you send them.                                                     *
 *                                                                         *
 * This program is distributed in the hope that it will be useful, but     *
 * WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU       *
 * General Public License v2.0 for more details at                         *
 * http://www.gnu.org/licenses/gpl-2.0.html , or in the COPYING file       *
 * included with Nmap.                                                     *
 *                                                                         *
 ***************************************************************************/


#include "ncrack.h"
#include "NcrackOps.h"
#include "utils.h"
#include "services.h"
#include "targets.h"
#include "TargetGroup.h"
#include "ServiceGroup.h"
#include "nsock.h"
#include "global_structures.h"
#include "modules.h"
#include "ncrack_error.h"
#include "output.h"
#include "ncrack_tty.h"
#include <time.h>
#include <vector>

#ifdef WIN32
#include "winfix.h"
#endif

#define DEFAULT_CONNECT_TIMEOUT 5000
#define DEFAULT_USERNAME_FILE "./username.lst"
#define DEFAULT_PASSWORD_FILE "./password.lst"

/* (in milliseconds) every such interval we poll for interactive user input */
#define KEYPRESSED_INTERVAL 500 

extern NcrackOps o;
using namespace std;

/* global lookup table for available services */
vector <global_service> ServicesTable;
/* global login and pass array */
vector <char *> UserArray;
vector <char *> PassArray;


/* schedule additional connections */
static int ncrack_probes(nsock_pool nsp, ServiceGroup *SG);
/* ncrack initialization */
static int ncrack(ServiceGroup *SG);
/* Poll for interactive user input every time this timer is called. */
static void status_timer_handler(nsock_pool nsp, nsock_event nse, void *mydata);

/* module name demultiplexor */
static void call_module(nsock_pool nsp, Connection* con);

static void load_login_file(const char *filename, int mode);
enum mode { USER, PASS };


static void print_usage(void);
static void lookup_init(const char *const filename);
static char *grab_next_host_spec(FILE *inputfd, int argc, char **argv);


static void
print_usage(void)
{
  log_write(LOG_STDOUT, "%s %s ( %s )\n"
      "Usage: ncrack [Options] {target specification}\n"
      "TARGET SPECIFICATION:\n"
      "  Can pass hostnames, IP addresses, networks, etc.\n"
      "  Ex: scanme.nmap.org, microsoft.com/24, 192.168.0.1; 10.0.0-255.1-254\n"
      "  -iL <inputfilename>: Input from list of hosts/networks\n"
      "  --exclude <host1[,host2][,host3],...>: Exclude hosts/networks\n"
      "  --excludefile <exclude_file>: Exclude list from file\n"
      "SERVICE SPECIFICATION:\n"
      "  Can pass target specific services in <service>://target (standard) notation or\n"
      "  using -p which will be applied to all hosts in non-standard notation.\n"
      "  Service arguments can be specified to be host-specific, type of service-specific\n"
      "  (-m) or global (-g). Ex: ssh://10.0.0.10,al=10,cl=30 -m ssh:at=50 -g cd=3000\n"
      "  Ex2: ncrack -p ssh,ftp:3500,25 10.0.0.10 scanme.nmap.org\n"
      "  -p <service-list>: services will be applied to all non-standard notation hosts\n"
      "  -m <service>:<options>: options will be applied to all services of this type\n"
      "  -g <options>: options will be applied to every service globally\n"
      "  Available Options:\n"
      "   Timing:\n"
      "    cl (min connection limit): minimum number of concurrent parallel connections\n"
      "    CL (max connection limit): maximum number of concurrent parallel connections\n"
      "    at (authentication tries): authentication attempts per connection\n"
      "    cd (connection delay): delay between each connection initiation (in milliseconds)\n"
      "    cr (connection retries): caps number of service connection attempts\n"
      "TIMING AND PERFORMANCE:\n"
      "  Options which take <time> are in milliseconds, unless you append 's'\n"
      "  (seconds), 'm' (minutes), or 'h' (hours) to the value (e.g. 30m).\n"
      "  -T<0-5>: Set timing template (higher is faster)\n"
      "  --connection-limit <number>: threshold for total concurrent connections\n"
  //    "  --host-timeout <time>: Give up on target after this long\n"
      "AUTHENTICATION:\n"
      "  -U <filename>: username file\n"
      "  -P <filename>: password file\n"
      "  --passwords-first: Iterate password list for each username. Default is opposite.\n"
      "OUTPUT:\n"
      "  -oN/-oX/-oG <file>: Output scan in normal, XML, and Grepable format,\n"
      "  respectively, to the given filename.\n"
      "  -oA <basename>: Output in the three major formats at once\n"
      "  -v: Increase verbosity level (use twice or more for greater effect)\n"
      "  -d[level]: Set or increase debugging level (Up to 9 is meaningful)\n"
      "  --log-errors: Log errors/warnings to the normal-format output file\n"
      "  --append-output: Append to rather than clobber specified output files\n"
      "MISC:\n"
      "  -sL or --list: only list hosts and services\n"
      "  -V: Print version number\n"
      "  -h: Print this help summary page.\n", NCRACK_NAME, NCRACK_VERSION, NCRACK_URL);
  exit(EXIT_FAILURE);
}


static void
lookup_init(const char *const filename)
{
  char line[1024];
  char servicename[128], proto[16];
  u16 portno;
  FILE *fp;
  vector <global_service>::iterator vi;
  global_service temp;

  memset(&temp, 0, sizeof(temp));
  temp.timing.min_connection_limit = -1;
  temp.timing.max_connection_limit = -1;
  temp.timing.auth_tries = -1;
  temp.timing.connection_delay = -1;
  temp.timing.connection_retries = -1;

  fp = fopen(filename, "r");
  if (!fp) 
    fatal("%s: failed to open file %s for reading!", __func__, filename);

  while (fgets(line, sizeof(line), fp)) {
    if (*line == '\n' || *line == '#')
      continue;

    if (sscanf(line, "%127s %hu/%15s", servicename, &portno, proto) != 3)
      fatal("invalid ncrack-services file: %s", filename);

    temp.lookup.portno = portno;
    temp.lookup.proto = str2proto(proto);
    temp.lookup.name = strdup(servicename);

    for (vi = ServicesTable.begin(); vi != ServicesTable.end(); vi++) {
      if ((vi->lookup.portno == temp.lookup.portno) && (vi->lookup.proto == temp.lookup.proto)
          && !(strcmp(vi->lookup.name, temp.lookup.name))) {
        if (o.debugging)
          error("Port %d proto %s is duplicated in services file %s", 
              portno, proto, filename);
        continue;
      }
    }

    ServicesTable.push_back(temp);
  }

  fclose(fp);
}




static char *
grab_next_host_spec(FILE *inputfd, int argc, char **argv)
{
  static char host_spec[1024];
  unsigned int host_spec_index;
  int ch;

  if (!inputfd) {
    return ((optind < argc) ? argv[optind++] : NULL);
  } else { 
    host_spec_index = 0;
    while((ch = getc(inputfd)) != EOF) {
      if (ch == ' ' || ch == '\r' || ch == '\n' || ch == '\t' || ch == '\0') {
        if (host_spec_index == 0)
          continue;
        host_spec[host_spec_index] = '\0';
        return host_spec;
      } else if (host_spec_index < sizeof(host_spec) / sizeof(char) -1) {
        host_spec[host_spec_index++] = (char) ch;
      } else fatal("One of the host_specifications from your input file "
          "is too long (> %d chars)", (int) sizeof(host_spec));
    }
    host_spec[host_spec_index] = '\0';
  }
  if (!*host_spec) 
    return NULL;
  return host_spec;
}



static void
load_login_file(const char *filename, int mode)
{
  char line[1024];
  char *tmp;
  FILE *fd;
  vector <char *> *p = NULL;

  if (!strcmp(filename, "-"))
    fd = stdin;
  else {    
    fd = fopen(filename, "r");
    if (!fd) 
      fatal("Failed to open input file %s for reading!", filename);
  }

  if (mode == USER)
    p = &UserArray;
  else if (mode == PASS)
    p = &PassArray;
  else 
    fatal("%s invalid mode specified!", __func__);

  while (fgets(line, sizeof(line), fd)) {
    if (*line == '\n')
      continue;
    tmp = Strndup(line, strlen(line) - 1);
    p->push_back(tmp);
  }
}



static void
call_module(nsock_pool nsp, Connection *con)
{
  char *name = con->service->name;

  /* initialize connection state variables */
  con->check_closed = false;
  con->auth_complete = false;
  con->peer_alive = false;
  con->finished_normally = false;

  if (!strcmp(name, "ftp"))
    ncrack_ftp(nsp, con);
  else if (!strcmp(name, "telnet"))
    ncrack_telnet(nsp, con);
  else if (!strcmp(name, "ssh"))
    ;//ncrack_ssh(nsp, nsi, con);
  else
    fatal("Invalid service module: %s", name);
}



int main(int argc, char **argv)
{
  ts_spec spec;

  FILE *inputfd = NULL;
  char *machinefilename = NULL;
  char *normalfilename = NULL;
  char *xmlfilename = NULL;
  unsigned long l;

  char *host_spec = NULL;
  Target *currenths = NULL;
  vector <Target *> Targets;        /* targets to be ncracked */
  vector <Target *>::iterator Tvi;

  ServiceGroup *SG;                 /* all services to be ncracked */
  list <Service *>::iterator li;

  vector <Service *>Services;       /* temporary services vector */
  vector <Service *>::iterator Svi; /* iterator for services vector */
  Service *service;

  vector <service_lookup *> services_cmd;
  vector <service_lookup *>::iterator SCvi;

  char *glob_options = NULL;  /* for -g option */
  timing_options timing;      /* for -T option */

  /* time variables */
  struct tm *tm;
  time_t now;
  char tbuf[128];

  /* exclude-specific variables */
  FILE *excludefd = NULL;
  char *exclude_spec = NULL;
  TargetGroup *exclude_group = NULL;


  /* getopt-specific */
  int arg;
  int option_index;
  extern char *optarg;
  extern int optind;
  struct option long_options[] =
  {
    {"list", no_argument, 0, 0},
    {"services", required_argument, 0, 'p'},
    {"version", no_argument, 0, 'V'},
    {"verbose", no_argument, 0, 'v'},
    {"debug", optional_argument, 0, 'd'},
    {"help", no_argument, 0, 'h'},
    {"timing", required_argument, 0, 'T'},
    {"excludefile", required_argument, 0, 0},
    {"exclude", required_argument, 0, 0},
    {"iL", required_argument, 0, 'i'},
    {"oA", required_argument, 0, 0},  
    {"oN", required_argument, 0, 0},
    {"oM", required_argument, 0, 0},  
    {"oG", required_argument, 0, 0},  
    {"oX", required_argument, 0, 0},  
    {"host_timeout", required_argument, 0, 0},
    {"host-timeout", required_argument, 0, 0},
    {"append_output", no_argument, 0, 0},
    {"append-output", no_argument, 0, 0},
    {"log_errors", no_argument, 0, 0},
    {"log-errors", no_argument, 0, 0},
    {"connection_limit", required_argument, 0, 0},
    {"connection-limit", required_argument, 0, 0},
    {"passwords_first", no_argument, 0, 0},
    {"passwords-first", no_argument, 0, 0},
    {0, 0, 0, 0}
  };

  if (argc < 2)
    print_usage();

  /* Initialize available services' lookup table */
  lookup_init("ncrack-services");

#if WIN32
  win_init();
#endif


  now = time(NULL);
  tm = localtime(&now);

  /* Argument parsing */
  optind = 1;
  while((arg = getopt_long_only(argc, argv, "d:g:hi:U:P:m:o:p:s:T:vV", long_options,
          &option_index)) != EOF) {
    switch(arg) {
      case 0:
        if (!strcmp(long_options[option_index].name, "excludefile")) {
          if (exclude_spec)
            fatal("--excludefile and --exclude options are mutually exclusive.");
          excludefd = fopen(optarg, "r");
          if (!excludefd)
            fatal("Failed to open exclude file %s for reading", optarg);
        } else if (!strcmp(long_options[option_index].name, "exclude")) {
          if (excludefd)
            fatal("--excludefile and --exclude options are mutually exclusive.");
          exclude_spec = strdup(optarg);

        } else if (!optcmp(long_options[option_index].name, "host-timeout")) {
          l = tval2msecs(optarg);
          if (l <= 1500)
            fatal("--host-timeout is specified in milliseconds unless you "
                "qualify it by appending 's', 'm', or 'h'. The value must be greater "
                "than 1500 milliseconds");
          o.host_timeout = l;
          if (l < 30000) 
            error("host-timeout is given in milliseconds, so you specified less "
                "than 30 seconds (%lims). This is allowed but not recommended.", l);
        } else if (!strcmp(long_options[option_index].name, "services")) {
          parse_services(optarg, services_cmd);
        } else if (!strcmp(long_options[option_index].name, "list")) {
          o.list_only++;
        } else if (!optcmp(long_options[option_index].name, "connection-limit")) {
          o.connection_limit = atoi(optarg);
        } else if (!optcmp(long_options[option_index].name, "passwords-first")) {
          o.passwords_first = true;
        } else if (!optcmp(long_options[option_index].name, "log-errors")) {
          o.log_errors = true;
        } else if (!optcmp(long_options[option_index].name, "append-output")) {
          o.append_output = true;
        } else if (strcmp(long_options[option_index].name, "oN") == 0) {
          normalfilename = logfilename(optarg, tm);
        } else if (strcmp(long_options[option_index].name, "oG") == 0 ||
            strcmp(long_options[option_index].name, "oM") == 0) {
          machinefilename = logfilename(optarg, tm);
        } else if (strcmp(long_options[option_index].name, "oX") == 0) {
          xmlfilename = logfilename(optarg, tm);
        } else if (strcmp(long_options[option_index].name, "oA") == 0) {
          char buf[MAXPATHLEN];
          Snprintf(buf, sizeof(buf), "%s.ncrack", logfilename(optarg, tm));
          normalfilename = strdup(buf);
          Snprintf(buf, sizeof(buf), "%s.gncrack", logfilename(optarg, tm));
          machinefilename = strdup(buf);
          Snprintf(buf, sizeof(buf), "%s.xml", logfilename(optarg, tm));
          xmlfilename = strdup(buf);
        }
        break;
      case 'd': 
        if (optarg)
          o.debugging = o.verbose = atoi(optarg);
        else 
          o.debugging++; o.verbose++;
        break;
      case 'g':
        glob_options = strdup(optarg);
        o.global_options = true;
        break;
      case 'h':   /* help */
        print_usage();
        break;
      case 'i': 
        if (inputfd)
          fatal("Only one input filename allowed");
        if (!strcmp(optarg, "-"))
          inputfd = stdin;
        else {    
          inputfd = fopen(optarg, "r");
          if (!inputfd) 
            fatal("Failed to open input file %s for reading", optarg);
        }
        break;
      case 'U':
        load_login_file(optarg, USER);
        break;
      case 'P':
        load_login_file(optarg, PASS);
        break;
      case 'm':
        parse_module_options(optarg);
        break;
      case 'o':
        normalfilename = logfilename(optarg, tm);
        break;
      case 'p':   /* services */
        parse_services(optarg, services_cmd); 
        break;
      case 's': /* only list hosts */
        if (*optarg == 'L')
          o.list_only = true;
        else 
          fatal("Illegal argument for option '-s' Did you mean -sL?");
        break;
      case 'T': /* timing template */
        if (*optarg == '0' || (strcasecmp(optarg, "Paranoid") == 0)) {
          o.timing_level = 0;
        } else if (*optarg == '1' || (strcasecmp(optarg, "Sneaky") == 0)) {
          o.timing_level = 1;
        } else if (*optarg == '2' || (strcasecmp(optarg, "Polite") == 0)) {
          o.timing_level = 2;
        } else if (*optarg == '3' || (strcasecmp(optarg, "Normal") == 0)) {
          o.timing_level = 3;
        } else if (*optarg == '4' || (strcasecmp(optarg, "Aggressive") == 0)) {
          o.timing_level = 4;
        } else if (*optarg == '5' || (strcasecmp(optarg, "Insane") == 0)) {
          o.timing_level = 5;
        } else {
          fatal("Unknown timing mode (-T argument).  Use either \"Paranoid\", \"Sneaky\", "
              "\"Polite\", \"Normal\", \"Aggressive\", \"Insane\" or a number from 0 "
              " (Paranoid) to 5 (Insane)");
        }
        break;
      case 'V': 
        log_write(LOG_STDOUT, "\n%s version %s ( %s )\n", NCRACK_NAME, NCRACK_VERSION, NCRACK_URL);
        exit(EXIT_SUCCESS);
        break;
      case 'v':
        o.verbose++;
        break;
      case '?':   /* error */
        print_usage();
    }
  }
  
  /* Initialize tty for interactive output */
  tty_init();

  /* Open the log files, now that we know whether the user wants them appended
     or overwritten */
  if (normalfilename) {
    log_open(LOG_NORMAL, normalfilename);
    free(normalfilename);
  }
  if (machinefilename) {
    log_open(LOG_MACHINE, machinefilename);
    free(machinefilename);
  }
  if (xmlfilename) {
    log_open(LOG_XML, xmlfilename);
    free(xmlfilename);
  }

  if (UserArray.empty())
    load_login_file(DEFAULT_USERNAME_FILE, USER);
  if (PassArray.empty())
    load_login_file(DEFAULT_PASSWORD_FILE, PASS);

  /* Prepare -T option (3 is default) */
  prepare_timing_template(&timing);

  now = time(NULL);
  tm = localtime(&now);
  if (strftime(tbuf, sizeof(tbuf), "%Y-%m-%d %H:%M %Z", tm) <= 0)
    fatal("Unable to properly format time");
  log_write(LOG_STDOUT, "\nStarting %s %s ( %s ) at %s\n", NCRACK_NAME, NCRACK_VERSION, NCRACK_URL, tbuf);

  o.setaf(AF_INET);


  /* lets load our exclude list */
  if ((NULL != excludefd) || (NULL != exclude_spec)) {
    exclude_group = load_exclude(excludefd, exclude_spec);

    if (o.debugging > 3)
      dumpExclude(exclude_group);

    if ((FILE *)NULL != excludefd)
      fclose(excludefd);
    if ((char *)NULL != exclude_spec)
      free(exclude_spec);
  }


  SG = new ServiceGroup();
  SG->connection_limit = o.connection_limit;

  while ((host_spec = grab_next_host_spec(inputfd, argc, argv))) {

    /* preparse and separate host - service */
    spec = parse_services_target(host_spec);

    // log_write(LOG_STDOUT, "%s://%s:%s?%s\n", spec.service_name, spec.host_expr, 
    //    spec.portno, spec.service_options);

    if (spec.service_name) {
      service = new Service();
      service->name = strdup(spec.service_name);
      service->UserArray = &UserArray;
      service->PassArray = &PassArray;
      Services.push_back(service);
    } else {  /* -p option */
      for (SCvi = services_cmd.begin(); SCvi != services_cmd.end(); SCvi++) {
        service = new Service();
        service->name = (*SCvi)->name;
        service->portno = (*SCvi)->portno;
        service->proto = (*SCvi)->proto;
        service->UserArray = &UserArray;
        service->PassArray = &PassArray;
        Services.push_back(service);
      }
    }


    for (Svi = Services.begin(); Svi != Services.end(); Svi++) {
      /* first apply timing template */
      apply_timing_template(*Svi, &timing);
      /* then apply global options -g if they exist */
      if (o.global_options) 
        apply_host_options(*Svi, glob_options);
      /* then apply options from ServiceTable (-m option) */
      apply_service_options(*Svi);
    }

    /* finally, if they have been specified, apply options from host */
    if (spec.service_options)
      apply_host_options(Services[0], spec.service_options);
    if (spec.portno)
      Services[0]->portno = str2port(spec.portno);

    while ((currenths = nexthost(spec.host_expr, exclude_group))) {
      for (Tvi = Targets.begin(); Tvi != Targets.end(); Tvi++) {
        if (!(strcmp((*Tvi)->NameIP(), currenths->NameIP())))
          break;
      }
      if (Tvi == Targets.end())
        Targets.push_back(currenths);
      else 
        currenths = *Tvi;

      for (Svi = Services.begin(); Svi != Services.end(); Svi++) {
        service = new Service(**Svi);

        service->target = currenths;
        /* check for duplicates */
        for (li = SG->services_active.begin(); li != SG->services_active.end(); li++) {
          if (!strcmp((*li)->target->NameIP(), currenths->NameIP()) &&
              (!strcmp((*li)->name, service->name)) && ((*li)->portno == service->portno))
            fatal("Duplicate service %s for target %s !", service->name, currenths->NameIP());
        }
        SG->services_active.push_back(service);
        SG->total_services++;
      }
    }
    Services.clear();
    clean_spec(&spec);
  }

  if (o.list_only) {
    if (o.debugging > 3) {
      log_write(LOG_PLAIN, "\n=== Timing Template ===\n");
      log_write(LOG_PLAIN, "cl=%ld, CL=%ld, at=%ld, cd=%ld, cr=%ld\n",
          timing.min_connection_limit, timing.max_connection_limit,
          timing.auth_tries, timing.connection_delay, timing.connection_retries);
      log_write(LOG_PLAIN, "\n=== ServicesTable ===\n");
      for (unsigned int i = 0; i < ServicesTable.size(); i++) {
        log_write(LOG_PLAIN, "%s:%hu cl=%ld, CL=%ld, at=%ld, cd=%ld, cr=%ld\n", 
            ServicesTable[i].lookup.name,
            ServicesTable[i].lookup.portno,
            ServicesTable[i].timing.min_connection_limit,
            ServicesTable[i].timing.max_connection_limit,
            ServicesTable[i].timing.auth_tries,
            ServicesTable[i].timing.connection_delay,
            ServicesTable[i].timing.connection_retries);
      }
    }
    log_write(LOG_PLAIN, "\n=== Targets ===\n");
    for (unsigned int i = 0; i < Targets.size(); i++) {
      log_write(LOG_PLAIN, "Host: %s", Targets[i]->NameIP());
      if (Targets[i]->targetname)
        log_write(LOG_PLAIN, " ( %s ) ", Targets[i]->targetname);
      log_write(LOG_PLAIN, "\n");
      for (li = SG->services_active.begin(); li != SG->services_active.end(); li++) {
        if ((*li)->target == Targets[i]) 
          log_write(LOG_PLAIN, "  %s:%hu cl=%ld, CL=%ld, at=%ld, cd=%ld, cr=%ld\n", 
              (*li)->name, (*li)->portno, (*li)->min_connection_limit,
              (*li)->max_connection_limit, (*li)->auth_tries, 
              (*li)->connection_delay, (*li)->connection_retries);
      }
    }
  } else {
    if (!SG->total_services)
      fatal("No services specified!");

    SG->last_accessed = SG->services_active.end();
    /* Ncrack 'em all! */
    ncrack(SG);
  }

  /* Free all of the Targets */
  while(!Targets.empty()) {
    currenths = Targets.back();
    delete currenths;
    Targets.pop_back();
  }
  delete SG;

  log_write(LOG_STDOUT, "\nNcrack finished.\n");
  exit(EXIT_SUCCESS);
}


/* 
 * It handles module endings
 */
void
ncrack_module_end(nsock_pool nsp, void *mydata)
{
  Connection *con = (Connection *) mydata;
  ServiceGroup *SG = (ServiceGroup *) nsp_getud(nsp);
  Service *serv = con->service;
  nsock_iod nsi = con->niod;
  struct timeval now;
  int pair_ret;

  con->login_attempts++;
  con->auth_complete = true;
  serv->total_attempts++;
  serv->finished_attempts++;

  if (serv->just_started)
    serv->supported_attempts++;

  serv->auth_rate_meter.update(1, NULL);

  gettimeofday(&now, NULL);
  if (!serv->just_started && TIMEVAL_MSEC_SUBTRACT(now, serv->last_auth_rate.time) >= 500) {
    double current_rate = serv->auth_rate_meter.getCurrentRate();
    if (o.debugging) 
      log_write(LOG_STDOUT, "%s last: %.2f current %.2f parallelism %ld\n", serv->HostInfo(),
          serv->last_auth_rate.rate, current_rate, serv->ideal_parallelism);
    if (current_rate < serv->last_auth_rate.rate + 3) {
      if (serv->ideal_parallelism + 3 < serv->max_connection_limit)
        serv->ideal_parallelism += 3;
      else 
        serv->ideal_parallelism = serv->max_connection_limit;
      if (o.debugging)
        log_write(LOG_STDOUT, "%s Increasing connection limit to: %ld\n", 
            serv->HostInfo(), serv->ideal_parallelism);
    }
    serv->last_auth_rate.time = now;
    serv->last_auth_rate.rate = current_rate;
  }

  /* If login pair was extracted from pool, permanently remove it from it. */
  if (con->from_pool && !serv->isMirrorPoolEmpty()) {
    serv->removeFromPool(con->user, con->pass);
    con->from_pool = false;
  }

  /*
   * Check if we had previously surpassed imposed connection limit so that
   * we remove service from 'services_full' list to 'services_active' list.
   */
  if (serv->list_full && serv->active_connections < serv->ideal_parallelism)
    SG->moveServiceToList(serv, &SG->services_active);

  ncrack_probes(nsp, SG);

  /* 
   * If we need to check whether peer is alive or not we do the following:
   * Since there is no portable way to check if the peer has closed the
   * connection or not (hence we are in CLOSE_WAIT state), issue a read call
   * with a very small timeout and check if nsock timed out (host hasn't closed
   * connection yet) or returned an EOF (host sent FIN making active close)
   * Note, however that the connection might have already indicated that the
   * peer is alive (for example telnetd sends the next login prompt along with the
   * authentication results, denoting that it immediately expects another
   * authentication attempt), so in that case we need to get the next login pair
   * only and make no additional check.
   */
  if (con->peer_alive) {
    if (con->login_attempts < serv->auth_tries
        && (pair_ret = serv->getNextPair(&con->user, &con->pass)) != -1) {
      if (pair_ret == 1)
        con->from_pool = true;
      nsock_timer_create(nsp, ncrack_timer_handler, 0, con);
    } else
      ncrack_connection_end(nsp, con);
  } else {
    /* We need to check if host is alive only on first timing
     * probe. Thereafter we can use the 'supported_attempts'.
     */
    if (serv->just_started) {
      con->check_closed = true;
      nsock_read(nsp, nsi, ncrack_read_handler, 100, con);
    } else if (con->login_attempts <= serv->auth_tries &&
        con->login_attempts <= serv->supported_attempts)
      call_module(nsp, con);
  }

}


void
ncrack_connection_end(nsock_pool nsp, void *mydata)
{
  Connection *con = (Connection *) mydata;
  Service *serv = con->service;
  nsock_iod nsi = con->niod;
  ServiceGroup *SG = (ServiceGroup *) nsp_getud(nsp);
  list <Connection *>::iterator li;
  const char *hostinfo = serv->HostInfo();


  if (con->close_reason == READ_TIMEOUT) {
    serv->appendToPool(con->user, con->pass);
    if (serv->list_stalled)
      SG->moveServiceToList(serv, &SG->services_active);
    if (o.debugging)
      error("%s nsock READ timeout!", hostinfo);

  } else if (con->close_reason == READ_EOF) {
    /* 
     * Check if we are on the point where peer might close at any moment (usually
     * we set 'peer_might_close' after writing the password on the network and
     * before issuing the next read call), so that this connection ending was
     * actually expected.
     */
    if (con->peer_might_close) {
      /* If we are the first special timing probe, then increment the number of
       * server-allowed authentication attempts per connection
       */
      if (serv->just_started)
        serv->supported_attempts++;

      serv->total_attempts++;
      serv->finished_attempts++;

      if (o.debugging > 6)
        log_write(LOG_STDOUT, "%s Failed %s %s\n", hostinfo, con->user, con->pass);

    } else if (!con->auth_complete) {
      serv->appendToPool(con->user, con->pass);
      if (serv->list_stalled)
        SG->moveServiceToList(serv, &SG->services_active);

      /* Now this is strange: peer closed on us in the middle of authentication.
       * This shouldn't happen, unless extreme network conditions are happening!
       */
      if (!serv->just_started && con->login_attempts < serv->supported_attempts) {
        if (o.debugging > 3)
          error("%s closed on us in the middle of authentication!", hostinfo);
      }
    }
    if (o.debugging > 5)
      error("%s Connection closed by peer", hostinfo);
  }

  /* 
   * If we are not the first timing probe and the authentication wasn't
   * completed (we double check that by seeing if we are inside the supported -by
   * the server- threshold of authentication attempts per connection), then we
   * take drastic action and drop the connection limit.
   */
  if (!serv->just_started && !con->auth_complete && !con->peer_might_close 
      && con->login_attempts < serv->supported_attempts) {
    serv->total_attempts++;
    // TODO: perhaps here we might want to differentiate between the two errors:
    // timeout and premature close, giving a unique drop value to each
    if (serv->ideal_parallelism - 5 >= serv->min_connection_limit)
      serv->ideal_parallelism -= 5;
    else 
      serv->ideal_parallelism = serv->min_connection_limit;

    if (o.debugging)
      log_write(LOG_STDOUT, "%s Dropping connection limit due to connection error to: %ld\n",
          hostinfo, serv->ideal_parallelism);
  }


  /* 
   * If that was our first connection, then calculate initial ideal_parallelism (which
   * was 1 previously) based on the box of min_connection_limit, max_connection_limit
   * and a default desired parallelism for each timing template.
   */
  if (serv->just_started == true) {
    serv->just_started = false;
    long desired_par = 1;
    if (o.timing_level == 0)
      desired_par = 1;
    else if (o.timing_level == 1)
      desired_par = 1;
    else if (o.timing_level == 2)
      desired_par = 4;
    else if (o.timing_level == 3)
      desired_par = 10;
    else if (o.timing_level == 4)
      desired_par = 30;
    else if (o.timing_level == 5)
      desired_par = 50;

    serv->ideal_parallelism = box(serv->min_connection_limit, serv->max_connection_limit, desired_par);
  }


  for (li = serv->connections.begin(); li != serv->connections.end(); li++) {
    if ((*li)->niod == nsi)
      break;
  } 
  if (li == serv->connections.end()) /* this shouldn't happen */
    fatal("%s: invalid niod!", __func__);

  SG->auth_rate_meter.update(con->login_attempts, NULL);

  nsi_delete(nsi, NSOCK_PENDING_SILENT);
  serv->connections.erase(li);

  serv->active_connections--;
  SG->active_connections--;


  /*
   * Check if we had previously surpassed imposed connection limit so that
   * we remove service from 'services_full' list to 'services_active' list.
   */
  if (serv->list_full && serv->active_connections < serv->ideal_parallelism)
    SG->moveServiceToList(serv, &SG->services_active);


  /*
   * If service was on 'services_finishing' (username list finished, pool empty
   * but still pending connections) then:
   * - if new pairs arrived into pool, move to 'services_active' again
   * - else if no more connections are pending, move to 'services_finished'
   */
  if (serv->list_finishing) {
    if (!serv->isMirrorPoolEmpty())
      SG->moveServiceToList(serv, &SG->services_active);
    else if (!serv->active_connections)
      SG->moveServiceToList(serv, &SG->services_finished);
  }

  if (o.debugging)
    log_write(LOG_STDOUT, "%s Attempts: total %d completed %d supported %d --- rate %.2f \n", 
        serv->HostInfo(), serv->total_attempts, serv->finished_attempts, serv->supported_attempts,
        SG->auth_rate_meter.getCurrentRate());

  /* Check if service finished for good. */
  if (serv->loginlist_fini && serv->isMirrorPoolEmpty() && !serv->active_connections && !serv->list_finished)
    SG->moveServiceToList(serv, &SG->services_finished);

  /* see if we can initiate some more connections */
  ncrack_probes(nsp, SG);

}


void
ncrack_read_handler(nsock_pool nsp, nsock_event nse, void *mydata)
{
  enum nse_status status = nse_status(nse);
  enum nse_type type = nse_type(nse);
  ServiceGroup *SG = (ServiceGroup *) nsp_getud(nsp);
  Connection *con = (Connection *) mydata;
  Service *serv = con->service;
  int pair_ret;
  int nbytes;
  int err;
  char *str;
  const char *hostinfo = serv->HostInfo();


  assert(type == NSE_TYPE_READ);

  if (status == NSE_STATUS_SUCCESS) {

    str = nse_readbuf(nse, &nbytes);
    /* don't forget to free possibly previous allocated memory */
    if (con->buf) {
      free(con->buf);
      con->buf = NULL;
    }
    con->buf = (char *) safe_zalloc(nbytes + 1);
    memcpy(con->buf, str, nbytes);
    con->bufsize = nbytes;
    call_module(nsp, con);

  } else if (status == NSE_STATUS_TIMEOUT) {

    /* First check if we are just making sure the host hasn't closed
     * on us, and so we are still in ESTABLISHED state, instead of
     * CLOSE_WAIT - we do this by issuing a read call with a tiny timeout.
     * If we are still connected, then we can go on checking if we can make
     * another authentication attempt in this particular connection.
     */
    if (con->check_closed) {
      /* Make another authentication attempt only if:
       * 1. we hanen't surpassed the authentication limit per connection for this service
       * 2. we still have enough login pairs from the pool
       */
      if (con->login_attempts < serv->auth_tries
          && (pair_ret = serv->getNextPair(&con->user, &con->pass)) != -1) {
        if (pair_ret == 1)
          con->from_pool = true;
        call_module(nsp, con);
      } else {
        con->close_reason = READ_EOF;
        ncrack_connection_end(nsp, con);
      }
    } else {
      /* This is a normal timeout */
      con->close_reason = READ_TIMEOUT;
      ncrack_connection_end(nsp, con);  // should we always close connection or try to wait?
    }

  } else if (status == NSE_STATUS_EOF) {
    con->close_reason = READ_EOF;
    ncrack_connection_end(nsp, con);

  }  else if (status == NSE_STATUS_ERROR) {

    err = nse_errorcode(nse);
    if (o.debugging > 2)
      error("%s nsock READ error #%d (%s)", hostinfo, err, strerror(err));
    serv->appendToPool(con->user, con->pass);
    if (serv->list_stalled)
      SG->moveServiceToList(serv, &SG->services_active);
    ncrack_connection_end(nsp, con);

  } else if (status == NSE_STATUS_KILL) {
    error("%s nsock READ nse_status_kill", hostinfo);

  } else
    error("%s WARNING: nsock READ unexpected status %d", hostinfo, (int) status);

  return;
}




void
ncrack_write_handler(nsock_pool nsp, nsock_event nse, void *mydata)
{
  enum nse_status status = nse_status(nse);
  Connection *con = (Connection *) mydata;
  Service *serv = con->service;
  const char *hostinfo = serv->HostInfo();
  int err;

  if (status == NSE_STATUS_SUCCESS)
    call_module(nsp, con);
  else if (status == NSE_STATUS_ERROR) {
    err = nse_errorcode(nse);
    if (o.debugging > 2)
      error("%s nsock WRITE error #%d (%s)", hostinfo, err, strerror(err));
  } else if (status == NSE_STATUS_KILL) {
    error("%s nsock WRITE nse_status_kill\n", hostinfo);
  } else
    error("%s WARNING: nsock WRITE unexpected status %d", 
        hostinfo, (int) (status));

  return;
}


void
ncrack_timer_handler(nsock_pool nsp, nsock_event nse, void *mydata)
{
  enum nse_status status = nse_status(nse);
  Connection *con = (Connection *) mydata;
  Service *serv = con->service;
  const char *hostinfo = serv->HostInfo();

  if (status == NSE_STATUS_SUCCESS) {
    if (con->buf) {
      free(con->buf);
      con->buf = NULL;
    }
    call_module(nsp, con);
  }
  else 
    error("%s nsock Timer handler error!", hostinfo);

  return;
}




void
ncrack_connect_handler(nsock_pool nsp, nsock_event nse, void *mydata)
{
  enum nse_status status = nse_status(nse);
  enum nse_type type = nse_type(nse);
  ServiceGroup *SG = (ServiceGroup *) nsp_getud(nsp);
  Connection *con = (Connection *) mydata;
  Service *serv = con->service;
  const char *hostinfo = serv->HostInfo();
  int err;

  assert(type == NSE_TYPE_CONNECT || type == NSE_TYPE_CONNECT_SSL);

  // if (svc->target->timedOut(nsock_gettimeofday())) {
  //end_svcprobe(nsp, PROBESTATE_INCOMPLETE, SG, svc, nsi);
  if (status == NSE_STATUS_SUCCESS) {

#if HAVE_OPENSSL
    // TODO: handle ossl
#endif

    call_module(nsp, con);

  } else if (status == NSE_STATUS_TIMEOUT || status == NSE_STATUS_ERROR) {

    /* This is not good. connect() really shouldn't generally be timing out. */
    if (o.debugging > 2) {
      err = nse_errorcode(nse);
      error("%s nsock CONNECT response with status %s error: %s", hostinfo,
          nse_status2str(status), strerror(err));
    }
    serv->failed_connections++;
    serv->appendToPool(con->user, con->pass);

    /* Failure of connecting on first attempt means we should probably drop
     * the service for good. */
    if (serv->just_started) {
      SG->moveServiceToList(serv, &SG->services_finished);
    }
    if (serv->list_stalled)
      SG->moveServiceToList(serv, &SG->services_active);
    ncrack_connection_end(nsp, con);

  } else if (status == NSE_STATUS_KILL) {

    if (o.debugging)
      error("%s nsock CONNECT nse_status_kill", hostinfo);
    serv->appendToPool(con->user, con->pass);
    if (serv->list_stalled)
      SG->moveServiceToList(serv, &SG->services_active);
    ncrack_connection_end(nsp, con);

  } else
    error("%s WARNING: nsock CONNECT unexpected status %d", 
        hostinfo, (int) status);

  return;
}


/* 
 * Poll for interactive user input every time this timer is called.
 */
static void
status_timer_handler(nsock_pool nsp, nsock_event nse, void *mydata)
{
  enum nse_status status = nse_status(nse);
  ServiceGroup *SG = (ServiceGroup *) nsp_getud(nsp);

  if (keyWasPressed())
    SG->printStatusMessage();

  if (status != NSE_STATUS_SUCCESS)
    error("Nsock status timer handler error!");

  /* Reschedule timer for the next polling. */
  nsock_timer_create(nsp, status_timer_handler, KEYPRESSED_INTERVAL, NULL);

  return;
}




static int
ncrack_probes(nsock_pool nsp, ServiceGroup *SG)
{
  Service *serv;
  Connection *con;
  struct sockaddr_storage ss;
  size_t ss_len;
  list <Service *>::iterator li;
  struct timeval now;
  int pair_ret;
  char *login, *pass;
  const char *hostinfo;


  /* First check for every service if connection_delay time has already
   * passed since its last connection and move them back to 'services_active'
   * list if it has.
   */
  gettimeofday(&now, NULL);
  for (li = SG->services_wait.begin(); li != SG->services_wait.end(); li++) {
    if (TIMEVAL_MSEC_SUBTRACT(now, (*li)->last) >= (*li)->connection_delay) {
      li = SG->moveServiceToList(*li, &SG->services_active);
    }
  }

  if (SG->last_accessed == SG->services_active.end()) 
    li = SG->services_active.begin();
  else 
    li = SG->last_accessed++;


  while (SG->active_connections < SG->connection_limit
      && SG->services_finished.size() != SG->total_services
      && SG->services_active.size() != 0) {

    serv = *li;
    hostinfo = serv->HostInfo();

    //if (serv->target->timedOut(nsock_gettimeofday())) {
    // end_svcprobe(nsp, PROBESTATE_INCOMPLETE, SG, svc, NULL);  TODO: HANDLE
    //  goto next;
    // }

    /*
     * If the service's last connection was earlier than 'connection_delay'
     * milliseconds ago, then temporarily move service to 'services_wait' list
     */
    gettimeofday(&now, NULL);
    if (TIMEVAL_MSEC_SUBTRACT(now, serv->last) < serv->connection_delay) {
      li = SG->moveServiceToList(serv, &SG->services_wait);
      goto next;
    }

    /* If the service's active connections surpass its imposed connection limit
     * then don't initiate any more connections for it and also move service in
     * the services_full list so that it won't be reaccessed in this loop.
     */
    if (serv->active_connections >= serv->ideal_parallelism) {
      li = SG->moveServiceToList(serv, &SG->services_full);
      goto next;
    }


    /* 
     * To mark a service as completely finished, first make sure:
     * a) that the username list has finished being iterated through once
     * b) that the mirror pair pool, which holds temporary login pairs which
     *    are currently being used, is empty
     * c) that no pending connections are left
     * d) that the service hasn't already finished 
     */
    if (serv->loginlist_fini && serv->isMirrorPoolEmpty() && !serv->list_finished) {
      if (!serv->active_connections) {
        li = SG->moveServiceToList(serv, &SG->services_finished);
        goto next;
      } else {
        li = SG->moveServiceToList(serv, &SG->services_finishing);
        goto next;
      }
    }

    /* 
     * If the username list iteration has finished, then don't initiate another
     * connection until our pair_pool has at least one element to grab another
     * pair from.
     */
    if (serv->loginlist_fini && serv->isPoolEmpty() && !serv->isMirrorPoolEmpty()) {
      li = SG->moveServiceToList(serv, &SG->services_stalled);
      goto next;
    }

    if ((pair_ret = serv->getNextPair(&login, &pass)) == -1) {
      goto next;
    }

    if (o.debugging > 8)
      log_write(LOG_STDOUT, "%s Initiating new Connection\n", hostinfo);

    /* Schedule 1 connection for this service */
    con = new Connection(serv);

    if (pair_ret == 1)
      con->from_pool = true;
    con->user = login;
    con->pass = pass;

    if ((con->niod = nsi_new(nsp, serv)) == NULL) {
      fatal("Failed to allocate Nsock I/O descriptor in %s()", __func__);
    }
    gettimeofday(&now, NULL);
    serv->last = now;
    serv->connections.push_back(con);
    serv->active_connections++;
    SG->active_connections++;

    serv->target->TargetSockAddr(&ss, &ss_len);
    if (serv->proto == IPPROTO_TCP)
      nsock_connect_tcp(nsp, con->niod, ncrack_connect_handler, 
          DEFAULT_CONNECT_TIMEOUT, con,
          (struct sockaddr *)&ss, ss_len,
          serv->portno);
    else {
      assert(serv->proto == IPPROTO_UDP);
      nsock_connect_udp(nsp, con->niod, ncrack_connect_handler, 
          serv, (struct sockaddr *) &ss, ss_len,
          serv->portno);
    }

next:

    SG->last_accessed = li;
    if (li == SG->services_active.end() || ++li == SG->services_active.end())
      li = SG->services_active.begin();

  }
  return 0;
}



static int
ncrack(ServiceGroup *SG)
{
  /* nsock variables */
  struct timeval now;
  enum nsock_loopstatus loopret;
  list <Service *>::iterator li;
  nsock_pool nsp;
  int tracelevel = 0;
  int err;

  /* create nsock p00l */
  if (!(nsp = nsp_new(SG))) 
    fatal("Can't create nsock pool.");

  gettimeofday(&now, NULL);
  nsp_settrace(nsp, tracelevel, &now);

  /* must always find minimum delay in the beginning that all
   * services are under the 'services_active' list */
  SG->findMinDelay();

  /* initiate all authentication rate meters */
  SG->auth_rate_meter.start();
  for (li = SG->services_active.begin(); li != SG->services_active.end(); li++)
    (*li)->auth_rate_meter.start();

  /* 
   * Since nsock can delay between each event due to the targets being really slow,
   * we need a way to make sure that we always poll for interactive user input
   * regardless of the above case. Thus we schedule a special timer event that
   * happens every KEYPRESSED_INTERVAL milliseconds and which reschedules
   * itself every time its handler is called.
   */
  nsock_timer_create(nsp, status_timer_handler, KEYPRESSED_INTERVAL, NULL);

  ncrack_probes(nsp, SG);

  /* nsock loop */
  do {

    loopret = nsock_loop(nsp, (int) SG->min_connection_delay);
    if (loopret == NSOCK_LOOP_ERROR) {
      err = nsp_geterrorcode(nsp);
      fatal("Unexpected nsock_loop error. Error code %d (%s)", err, strerror(err));
    }
    ncrack_probes(nsp, SG);

  } while (SG->services_finished.size() != SG->total_services);

  if (o.debugging > 4)
    log_write(LOG_STDOUT, "nsock_loop returned %d\n", loopret);

  return 0;
}

