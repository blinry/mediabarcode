#include <sys/file.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <unistd.h>
#include <string.h>
#include <popt.h>
#include "nordlicht.h"
#include "version.h"

#ifdef _WIN32
// from http://stackoverflow.com/a/8514474/248734
char* strsep(char** stringp, const char* delim) {
    char* start = *stringp;
    char* p;

    p = (start != NULL) ? strpbrk(start, delim) : NULL;

    if (p == NULL) {
        *stringp = NULL;
    } else {
        *p = '\0';
        *stringp = p + 1;
    }

    return start;
}
#endif

typedef struct {
    const char *name;
    const char *description;
    nordlicht_style style;
} style;

const style style_table[] = {
    {"horizontal", "compress frames to vertical lines and append them", NORDLICHT_STYLE_HORIZONTAL},
    {"horizontalthird", "same as 'horizontal', but only use the middle third", NORDLICHT_STYLE_HORIZONTALTHIRD},
    {"vertical", "compress frames to horizontal lines and rotate them counterclockwise by 90 degrees", NORDLICHT_STYLE_VERTICAL},
    {"verticalthird", "same as 'vertical', but only use the middle third", NORDLICHT_STYLE_VERTICALTHIRD},
    {"slitscan", "take single columns while constantly moving to the right (and wrapping back to the left)", NORDLICHT_STYLE_SLITSCAN},
    {"slitscanthird", "same as 'slitscan', but only use the middle third", NORDLICHT_STYLE_SLITSCANTHIRD},
    {"middlecolumn", "take the middlemost column of each frame", NORDLICHT_STYLE_MIDDLECOLUMN},
    {"thumbnails", "display small thumbnails at regular intervals", NORDLICHT_STYLE_THUMBNAILS},
    {"thumbnailsthird", "same as 'thumbnails', but only use the middle third", NORDLICHT_STYLE_THUMBNAILSTHIRD},
    {"spectrogram", "spectrogram of the first audio track (not all sample formats are supported yet)", NORDLICHT_STYLE_SPECTROGRAM},
    {"time", "time scale, each section equals one minute", NORDLICHT_STYLE_TIME},
    {NULL, NULL, NORDLICHT_STYLE_LAST}
};

void print_error(const char *message, ...) {
    fprintf(stderr, "nordlicht: ");
    va_list arglist;
    va_start(arglist, message);
    vfprintf(stderr, message, arglist);
    va_end(arglist);
    fprintf(stderr, "\n");
}

const char *filename_ext(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot || dot == path) return "";
    return dot+1;
}

void print_help(const poptContext popt, const int ret) {
    poptPrintHelp(popt, ret == 0 ? stdout : stderr, 0);

    printf("\nStyles:\n");
    int i;
    for (i = 0; style_table[i].name; i++) {
        printf("  %-17s %s\n", style_table[i].name, style_table[i].description);
    }

    printf("\n\
Examples:\n\
  nordlicht video.mp4                                   generate video.mp4.nordlicht.png of default size\n\
  nordlicht video.mp4 -s slitscan                       use the 'slitscan' style, see above\n\
  nordlicht video.mp4 -w 1000 -h 1000 -o timebar.png    override size and name of the output file\n");

    exit(ret);
}

int main(const int argc, const char **argv) {
    int width = -1;
    int height = -1;
    int rows = 1;
    float start = 0.0;
    float end = 1.0;
    char *output_file = NULL;
    char *buffer_file = NULL;
    char *styles_string = NULL;
    nordlicht_strategy strategy;
    int free_output_file = 0;

    int quiet = 0;
    int help = 0;
    int version = 0;

    const struct poptOption optionsTable[] = {
        {"width", 'w', POPT_ARG_INT, &width, 0, "set the timebar's width (default: \"height*10\", or 1920 pixels, if both are undefined)", NULL},
        {"height", 'h', POPT_ARG_INT, &height, 0, "set the timebar's height (default: \"width/10\")", NULL},
        {"rows", 'r', POPT_ARG_INT, &rows, 0, "set the number of rows (default: 1)", NULL},
        {"output", 'o', POPT_ARG_STRING, &output_file, 0, "set output filename (default: VIDEOFILE.nordlicht.png)", "FILENAME"},
        {"buffer", 'b', POPT_ARG_STRING, &buffer_file, 0, "the internal raw 32-bit BGRA image buffer will be mmapped to this file, can be used for live display", "FILENAME"},
        {"style", 's', POPT_ARG_STRING, &styles_string, 0, "default is 'horizontal', see \"Styles\" section below. You can specify more than one style, separated by '+', to get multiple tracks", "STYLE"},
        {"start", '\0', POPT_ARG_FLOAT, &start, 0, "specify where to start the timebar (ratio between 0 and 1)", NULL},
        {"end", '\0', POPT_ARG_FLOAT, &end, 0, "specify where to end the timebar (ratio between 0 and 1)", NULL},
        {"quiet", 'q', 0, &quiet, 0, "don't show progress indicator", NULL},
        {"help", '\0', 0, &help, 0, "display this help and exit", NULL},
        {"version", '\0', 0, &version, 0, "output version information and exit", NULL},
        POPT_TABLEEND
    };

    const poptContext popt = poptGetContext(NULL, argc, argv, optionsTable, 0);
    poptSetOtherOptionHelp(popt, "[OPTION]... VIDEOFILE\n\nOptions:");

    if (argc == 1) {
        print_help(popt, 1);
    }

    int option;

    // The next line leaks 2 bytes, blame popt!
    while ((option = poptGetNextOpt(popt)) >= 0) { }

    if (option < -1) {
        fprintf(stderr, "nordlicht: %s: %s\n", poptBadOption(popt, POPT_BADOPTION_NOALIAS), poptStrerror(option));
        return 1;
    }

    if (version) {
      printf("nordlicht %s\n\nWritten by Sebastian Morr and contributors.\n", NORDLICHT_VERSION);
      return 0;
    }

    if (help) {
        print_help(popt, 0);
    }

    const char *filename = (char*)poptGetArg(popt);

    if (filename == NULL) {
        print_error("Please specify an input file.");
        exit(1);
    }

    if (poptGetArg(popt) != NULL) {
        print_error("Please specify only one input file.");
        exit(1);
    }

    if (output_file == NULL) {
        size_t len = snprintf(NULL, 0, "%s.nordlicht.png", filename) + 1;
        output_file = (char *) malloc(len);
        snprintf(output_file, len, "%s.nordlicht.png", filename);
        free_output_file = 1;
    }

    if (width == -1 && height != -1) {
        width = height*10;
    }
    if (height == -1 && width != -1) {
        height = width/10;
        if (height < 1) {
            height = 1;
        }
    }
    if (height == -1 && width == -1) {
        width = 1920;
        height = 192;
    }

    if (styles_string == NULL) {
        styles_string = "horizontal";
    }

    // count the occurrences of "+" in the styles_string
    const char *s = styles_string;
    int num_tracks;
    for (num_tracks=0; s[num_tracks]; s[num_tracks]=='+' ? num_tracks++ : *s++);
    num_tracks++;

    nordlicht_style *styles;
    styles = (nordlicht_style *) malloc(num_tracks * sizeof(nordlicht_style));

    const char *style_string;
    num_tracks = 0;
    while ((style_string = strsep(&styles_string, "+"))) {
        int i;
        for (i = 0; style_table[i].name; i++) {
            if (strcmp(style_string, style_table[i].name) == 0) {
                styles[num_tracks] = style_table[i].style;
                break;
            }
        }

        if (!style_table[i].name) {
            print_error("Unknown style '%s'. Use '--help' to display available styles.", style_string);
            exit(1);
        }
        num_tracks++;
    }

    const char *ext = filename_ext(output_file);
    if (strcmp(ext, "png") != 0) {
        fprintf(stderr, "nordlicht: Unsupported file extension '%s', will write a PNG.\n", ext);
    }

    if (buffer_file == NULL) {
        strategy = NORDLICHT_STRATEGY_FAST;
    } else {
        strategy = NORDLICHT_STRATEGY_LIVE;
    }

    // Interesting stuff begins here!

    nordlicht *n = nordlicht_init(filename, width, height);
    unsigned char *data = NULL;

    if (n == NULL) {
        print_error(nordlicht_error());
        exit(1);
    }

    nordlicht_set_rows(n, rows);
    nordlicht_set_start(n, start);
    nordlicht_set_end(n, end);
    nordlicht_set_styles(n, styles, num_tracks);
    nordlicht_set_strategy(n, strategy);

    if (nordlicht_error() != NULL) {
        print_error(nordlicht_error());
        exit(1);
    }

    if (buffer_file != NULL) {
        int fd = open(buffer_file, O_CREAT | O_TRUNC | O_RDWR, 0666);
        if (fd == -1) {
            print_error("Could not open '%s'.", buffer_file);
            exit(1);
        }
        if (ftruncate(fd, nordlicht_buffer_size(n)) == -1) {
            print_error("Could not truncate '%s'.", buffer_file);
            exit(1);
        }
        data = (unsigned char *) mmap(NULL, nordlicht_buffer_size(n), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (data == (void *) -1) {
            print_error("Could not mmap %d bytes.", nordlicht_buffer_size(n));
            exit(1);
        }
        nordlicht_set_buffer(n, data);
        close(fd);
    }

    int phase = -1;
    while(!nordlicht_done(n)) {
        if (nordlicht_generate_step(n) == 0) {
            if (! quiet) {
                float progress = nordlicht_progress(n);
                if (progress == 0) {
                    if (phase == -1) {
                        phase = 0;
                        printf("nordlicht: Building keyframe index... ");
                        fflush(stdout);
                    }
                } else {
                    if (phase == 0) {
                        phase = 1;
                        printf("done.\n");
                    }
                    printf("\rnordlicht: %02.0f%%", progress*100);
#ifdef DEBUG
                    printf("\n");
#endif
                    fflush(stdout);
                }
            }
        } else {
            print_error(nordlicht_error());
            exit(1);
        }
    }

    if (nordlicht_write(n, output_file) != 0) {
        print_error(nordlicht_error());
        exit(1);
    }

    free(styles);

    if (buffer_file != NULL) {
        munmap(data, nordlicht_buffer_size(n));
    }

    nordlicht_free(n);

    if (! quiet) {
        printf(" -> '%s'\n", output_file);
    }

    if (free_output_file) {
        free(output_file);
    }

    poptFreeContext(popt);
    return 0;
}
