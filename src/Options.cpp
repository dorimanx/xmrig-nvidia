/* XMRig
 * Copyright 2010      Jeff Garzik <jgarzik@pobox.com>
 * Copyright 2012-2014 pooler      <pooler@litecoinpool.org>
 * Copyright 2014      Lucas Jones <https://github.com/lucasjones>
 * Copyright 2014-2016 Wolf9466    <https://github.com/OhGodAPet>
 * Copyright 2016      Jay D Dee   <jayddee246@gmail.com>
 * Copyright 2016-2017 XMRig       <support@xmrig.com>
 *
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program. If not, see <http://www.gnu.org/licenses/>.
 */


#include <jansson.h>
#include <string.h>
#include <uv.h>


#ifdef _MSC_VER
#   include "getopt/getopt.h"
#else
#   include <getopt.h>
#endif


#include "Cpu.h"
#include "donate.h"
#include "net/Url.h"
#include "nvidia/cryptonight.h"
#include "Options.h"
#include "Platform.h"
#include "version.h"
#include "workers/GpuThread.h"


#ifndef ARRAY_SIZE
#   define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))
#endif


Options *Options::m_self = nullptr;


static char const usage[] = "\
Usage: " APP_ID " [OPTIONS]\n\
Options:\n\
  -o, --url=URL         URL of mining server\n\
  -O, --userpass=U:P    username:password pair for mining server\n\
  -u, --user=USERNAME   username for mining server\n\
  -p, --pass=PASSWORD   password for mining server\n\
  -k, --keepalive       send keepalived for prevent timeout (need pool support)\n\
  -r, --retries=N       number of times to retry before switch to backup server (default: 5)\n\
  -R, --retry-pause=N   time to pause between retries (default: 5)\n\
      --no-color        disable colored output\n\
      --donate-level=N  donate level, default 5%% (5 minutes in 100 minutes)\n\
      --user-agent      set custom user-agent string for pool\n\
  -B, --background      run the miner in the background\n\
  -c, --config=FILE     load a JSON-format configuration file\n\
  -l, --log-file=FILE   log all output to a file\n"
# ifdef HAVE_SYSLOG_H
"\
  -S, --syslog          use system log for output messages\n"
# endif
"\
      --nicehash        enable nicehash support\n\
      --print-time=N    print hashrate report every N seconds\n\
  -h, --help            display this help and exit\n\
  -V, --version         output version information and exit\n\
";


static char const short_options[] = "a:c:khBp:Px:r:R:s:T:o:u:O:Vl:S";


static struct option const options[] = {
    { "algo",            1, nullptr, 'a'  },
    { "background",      0, nullptr, 'B'  },
    { "config",          1, nullptr, 'c'  },
    { "donate-level",    1, nullptr, 1003 },
    { "help",            0, nullptr, 'h'  },
    { "keepalive",       0, nullptr ,'k'  },
    { "log-file",        1, nullptr, 'l'  },
    { "max-gpu-threads", 1, nullptr, 1200 },
    { "max-gpu-usage",   1, nullptr, 1004 },
    { "nicehash",        0, nullptr, 1006 },
    { "no-color",        0, nullptr, 1002 },
    { "pass",            1, nullptr, 'p'  },
    { "print-time",      1, nullptr, 1007 },
    { "retries",         1, nullptr, 'r'  },
    { "retry-pause",     1, nullptr, 'R'  },
    { "syslog",          0, nullptr, 'S'  },
    { "url",             1, nullptr, 'o'  },
    { "user",            1, nullptr, 'u'  },
    { "user-agent",      1, nullptr, 1008 },
    { "userpass",        1, nullptr, 'O'  },
    { "version",         0, nullptr, 'V'  },
    { 0, 0, 0, 0 }
};


static struct option const config_options[] = {
    { "algo",            1, nullptr, 'a' },
    { "background",      0, nullptr, 'B' },
    { "colors",          0, nullptr, 2000 },
    { "donate-level",    1, nullptr, 1003 },
    { "log-file",        1, nullptr, 'l' },
    { "max-gpu-threads", 1, nullptr, 1200 },
    { "max-gpu-usage",   1, nullptr, 1004 },
    { "print-time",      1, nullptr, 1007 },
    { "retries",         1, nullptr, 'r' },
    { "retry-pause",     1, nullptr, 'R' },
    { "syslog",          0, nullptr, 'S' },
    { "user-agent",      1, nullptr, 1008 },
    { 0, 0, 0, 0 }
};


static struct option const pool_options[] = {
    { "url",           1, nullptr, 'o'  },
    { "pass",          1, nullptr, 'p'  },
    { "user",          1, nullptr, 'u'  },
    { "userpass",      1, nullptr, 'O'  },
    { "keepalive",     0, nullptr ,'k'  },
    { "nicehash",      0, nullptr, 1006 },
    { 0, 0, 0, 0 }
};


static struct option const thread_options[] = {
    { "index",         1, nullptr, 3000 },
    { "threads",       1, nullptr, 3001 },
    { "blocks",        1, nullptr, 3002 },
    { "bfactor",       1, nullptr, 3003 },
    { "bsleep",        1, nullptr, 3004 },
    { 0, 0, 0, 0 }
};


static const char *algo_names[] = {
    "cryptonight",
#   ifndef XMRIG_NO_AEON
    "cryptonight-lite"
#   endif
};


Options *Options::parse(int argc, char **argv)
{
    Options *options = new Options(argc, argv);
    if (options->isReady()) {
        m_self = options;
        return m_self;
    }

    delete options;
    return nullptr;
}


bool Options::save()
{
    if (m_configName == nullptr) {
        return false;
    }

    uv_fs_t req;
    const int fd = uv_fs_open(uv_default_loop(), &req, m_configName, O_WRONLY, 0644, nullptr);
    if (fd < 0) {
        return false;
    }

    uv_fs_req_cleanup(&req);

    json_t *options = json_object();
    json_object_set(options, "background",   json_boolean(m_background));
    json_object_set(options, "colors",       json_boolean(m_colors));
    json_object_set(options, "donate-level", json_integer(m_donateLevel));
    json_object_set(options, "log-file",     m_logFile ? json_string(m_logFile) : json_null());
    json_object_set(options, "print-time",   json_integer(m_printTime));
    json_object_set(options, "retries",      json_integer(m_retries));
    json_object_set(options, "retry-pause",  json_integer(m_retryPause));

#   ifdef HAVE_SYSLOG_H
    json_object_set(options, "syslog", json_boolean(m_syslog));
#   endif

    json_t *threads = json_array();
    for (const GpuThread *thread : m_threads) {
        json_t *obj = json_object();
        json_object_set(obj, "index",   json_integer(thread->id()));
        json_object_set(obj, "threads", json_integer(thread->threads()));
        json_object_set(obj, "blocks",  json_integer(thread->blocks()));
        json_object_set(obj, "bfactor", json_integer(thread->bfactor()));
        json_object_set(obj, "bsleep",  json_integer(thread->bsleep()));

        json_array_append(threads, obj);
    }

    json_object_set(options, "threads", threads);

    json_t *pools = json_array();
    char tmp[256];

    for (const Url *url : m_pools) {
        json_t *obj = json_object();

        snprintf(tmp, sizeof(tmp) - 1, "%s:%d", url->host(), url->port());
        json_object_set(obj, "url",       json_string(tmp));
        json_object_set(obj, "user",      json_string(url->user()));
        json_object_set(obj, "pass",      json_string(url->password()));
        json_object_set(obj, "keepalive", json_boolean(url->isKeepAlive()));
        json_object_set(obj, "nicehash",  json_boolean(url->isNicehash()));

        json_array_append(pools, obj);
    }

    json_object_set(options, "pools", pools);

    json_dumpfd(options, fd, JSON_INDENT(4));
    uv_fs_close(uv_default_loop(), &req, fd, nullptr);
    uv_fs_req_cleanup(&req);

    return true;
}


const char *Options::algoName() const
{
    return algo_names[m_algo];
}


Options::Options(int argc, char **argv) :
    m_autoConf(false),
    m_background(false),
    m_colors(true),
    m_ready(false),
    m_syslog(false),
    m_configName(nullptr),
    m_logFile(nullptr),
    m_userAgent(nullptr),
    m_algo(0),
    m_algoVariant(0),
    m_donateLevel(kDonateLevel),
    m_maxGpuThreads(0),
    m_maxGpuUsage(100),
    m_printTime(60),
    m_retries(5),
    m_retryPause(5),
    m_threads(0)
{
    m_pools.push_back(new Url());

    int key;

    while (1) {
        key = getopt_long(argc, argv, short_options, options, NULL);
        if (key < 0) {
            break;
        }

        if (!parseArg(key, optarg)) {
            return;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "%s: unsupported non-option argument '%s'\n", argv[0], argv[optind]);
        return;
    }

    if (!m_pools[0]->isValid()) {
        parseConfig(Platform::defaultConfigName());
    }

    if (!m_pools[0]->isValid()) {
        fprintf(stderr, "No pool URL supplied. Exiting.\n");
        return;
    }

    m_algoVariant = Cpu::hasAES() ? AV1_AESNI : AV3_SOFT_AES;

    if (m_threads.empty()) {
        m_autoConf = true;
        GpuThread::autoConf(m_threads);

        for (GpuThread *thread : m_threads) {
            thread->limit(m_maxGpuUsage, m_maxGpuThreads);
        }
    }

    m_ready = true;
}


Options::~Options()
{
}


bool Options::parseArg(int key, const char *arg)
{
    switch (key) {
    case 'a': /* --algo */
        if (!setAlgo(arg)) {
            return false;
        }
        break;

    case 'o': /* --url */
        if (m_pools.size() > 1 || m_pools[0]->isValid()) {
            Url *url = new Url(arg);
            if (url->isValid()) {
                m_pools.push_back(url);
            }
            else {
                delete url;
            }
        }
        else {
            m_pools[0]->parse(arg);
        }

        if (!m_pools.back()->isValid()) {
            return false;
        }
        break;

    case 'O': /* --userpass */
        if (!m_pools.back()->setUserpass(arg)) {
            return false;
        }
        break;

    case 'u': /* --user */
        m_pools.back()->setUser(arg);
        break;

    case 'p': /* --pass */
        m_pools.back()->setPassword(arg);
        break;

    case 'l': /* --log-file */
        free(m_logFile);
        m_logFile = strdup(arg);
        m_colors = false;
        break;

    case 'r':  /* --retries */
    case 'R':  /* --retry-pause */
    case 't':  /* --threads */
    case 'v':  /* --av */
    case 1003: /* --donate-level */
    case 1004: /* --max-gpu-usage */
    case 1007: /* --print-time */
    case 1200: /* --max-gpu-threads */
        return parseArg(key, strtol(arg, nullptr, 10));

    case 'B':  /* --background */
    case 'k':  /* --keepalive */
    case 'S':  /* --syslog */
    case 1005: /* --safe */
    case 1006: /* --nicehash */
        return parseBoolean(key, true);

    case 1002: /* --no-color */
        return parseBoolean(key, false);

    case 'V': /* --version */
        showVersion();
        return false;

    case 'h': /* --help */
        showUsage(0);
        return false;

    case 'c': /* --config */
        parseConfig(arg);
        break;

    case 1008: /* --user-agent */
        free(m_userAgent);
        m_userAgent = strdup(arg);
        break;

    default:
        showUsage(1);
        return false;
    }

    return true;
}


bool Options::parseArg(int key, uint64_t arg)
{
    switch (key) {
        case 'r': /* --retries */
        if (arg < 1 || arg > 1000) {
            showUsage(1);
            return false;
        }

        m_retries = (int) arg;
        break;

    case 'R': /* --retry-pause */
        if (arg < 1 || arg > 3600) {
            showUsage(1);
            return false;
        }

        m_retryPause = (int) arg;
        break;

    case 't': /* --threads */
        if (arg < 1 || arg > 1024) {
            showUsage(1);
            return false;
        }

        //m_threads = arg;
        break;

    case 1003: /* --donate-level */
        if (arg < 1 || arg > 99) {
            showUsage(1);
            return false;
        }

        m_donateLevel = (int) arg;
        break;

    case 1004: /* --max-gpu-usage */
        if (arg < 1 || arg > 100) {
            showUsage(1);
            return false;
        }

        m_maxGpuUsage = (int) arg;
        break;

    case 1007: /* --print-time */
        if (arg > 1000) {
            showUsage(1);
            return false;
        }

        m_printTime = (int) arg;
        break;

    case 1200: /* --max-gpu-threads */
        m_maxGpuThreads = (int) arg;
        break;

    default:
        break;
    }

    return true;
}


bool Options::parseBoolean(int key, bool enable)
{
    switch (key) {
    case 'k': /* --keepalive */
        m_pools.back()->setKeepAlive(enable);
        break;

    case 'B': /* --background */
        m_background = enable;
        m_colors = enable ? false : m_colors;
        break;

    case 'S': /* --syslog */
        m_syslog = enable;
        m_colors = enable ? false : m_colors;
        break;

    case 1002: /* --no-color */
        m_colors = enable;
        break;

    case 1006: /* --nicehash */
        m_pools.back()->setNicehash(enable);
        break;

    case 2000: /* colors */
        m_colors = enable;
        break;

    default:
        break;
    }

    return true;
}


Url *Options::parseUrl(const char *arg) const
{
    auto url = new Url(arg);
    if (!url->isValid()) {
        delete url;
        return nullptr;
    }

    return url;
}


void Options::parseConfig(const char *fileName)
{
    uv_fs_t req;
    const int fd = uv_fs_open(uv_default_loop(), &req, fileName, O_RDONLY, 0644, nullptr);
    if (fd < 0) {
        fprintf(stderr, "unable to open %s: %s\n", fileName, uv_strerror(fd));
        return;
    }

    uv_fs_req_cleanup(&req);

    json_error_t err;
    json_t *config = json_loadfd(fd, 0, &err);

    uv_fs_close(uv_default_loop(), &req, fd, nullptr);
    uv_fs_req_cleanup(&req);

    if (!json_is_object(config)) {
        if (config) {
            json_decref(config);
            return;
        }

        if (err.line < 0) {
            fprintf(stderr, "%s\n", err.text);
        }
        else {
            fprintf(stderr, "%s:%d: %s\n", fileName, err.line, err.text);
        }

        return;
    }

    m_configName = strdup(fileName);

    for (size_t i = 0; i < ARRAY_SIZE(config_options); i++) {
        parseJSON(&config_options[i], config);
    }

    json_t *pools = json_object_get(config, "pools");
    if (json_is_array(pools)) {
        size_t index;
        json_t *value;

        json_array_foreach(pools, index, value) {
            if (json_is_object(value)) {
                for (size_t i = 0; i < ARRAY_SIZE(pool_options); i++) {
                    parseJSON(&pool_options[i], value);
                }
            }
        }
    }

    json_t *threads = json_object_get(config, "threads");
    if (json_is_array(threads)) {
        size_t index;
        json_t *value;

        json_array_foreach(threads, index, value) {
            if (json_is_object(value)) {
                parseThread(value);
            }
        }
    }


    json_decref(config);
}


void Options::parseJSON(const struct option *option, json_t *object)
{
    if (!option->name) {
        return;
    }

    json_t *val = json_object_get(object, option->name);
    if (!val) {
        return;
    }

    if (option->has_arg && json_is_string(val)) {
        parseArg(option->val, json_string_value(val));
    }
    else if (option->has_arg && json_is_integer(val)) {
        parseArg(option->val, json_integer_value(val));
    }
    else if (!option->has_arg && json_is_boolean(val)) {
        parseBoolean(option->val, json_is_true(val));
    }
}


void Options::parseThread(json_t *object)
{
    GpuThread *thread = new GpuThread();
    thread->setId((int) json_integer_value(json_object_get(object, "index")));
    thread->setThreads((int) json_integer_value(json_object_get(object, "threads")));
    thread->setBlocks((int) json_integer_value(json_object_get(object, "blocks")));
    thread->setBFactor((int) json_integer_value(json_object_get(object, "bfactor")));
    thread->setBSleep((int) json_integer_value(json_object_get(object, "bsleep")));

    if (thread->init()) {
        m_threads.push_back(thread);
        return;
    }

    delete thread;
}



void Options::showUsage(int status) const
{
    if (status) {
        fprintf(stderr, "Try \"" APP_ID "\" --help' for more information.\n");
    }
    else {
        printf(usage);
    }
}


void Options::showVersion()
{
    printf(APP_NAME " " APP_VERSION "\n built on " __DATE__

#   if defined(__clang__)
    " with clang " __clang_version__);
#   elif defined(__GNUC__)
    " with GCC");
    printf(" %d.%d.%d", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#   elif defined(_MSC_VER)
    " with MSVC");
    printf(" %d", MSVC_VERSION);
#   else
    );
#   endif

    printf("\n features:"
#   if defined(__i386__) || defined(_M_IX86)
    " i386"
#   elif defined(__x86_64__) || defined(_M_AMD64)
    " x86_64"
#   endif

#   if defined(__AES__) || defined(_MSC_VER)
    " AES-NI"
#   endif
    "\n");

    printf("\nlibuv/%s\n", uv_version_string());
    printf("libjansson/%s\n", JANSSON_VERSION);

    const int cudaVersion = cuda_get_runtime_version();
    printf("CUDA/%d.%d\n", cudaVersion / 1000, cudaVersion % 100);
}


bool Options::setAlgo(const char *algo)
{
    for (size_t i = 0; i < ARRAY_SIZE(algo_names); i++) {
        if (algo_names[i] && !strcmp(algo, algo_names[i])) {
            m_algo = (int) i;
            break;
        }

#       ifndef XMRIG_NO_AEON
        if (i == ARRAY_SIZE(algo_names) - 1 && !strcmp(algo, "cryptonight-light")) {
            m_algo = ALGO_CRYPTONIGHT_LITE;
            break;
        }
#       endif

        if (i == ARRAY_SIZE(algo_names) - 1) {
            showUsage(1);
            return false;
        }
    }

    return true;
}
