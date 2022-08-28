#define RED                                (0xff0000)
#define GREEN                              (0xff00)
#define BLUE                               (0xff)
#define YELLOW                             (RED|GREEN)
#define VIOLET                             (RED|BLUE)
#define TURQUOISE                          (GREEN|BLUE)
#define WHITE                              (RED|GREEN|BLUE)
#define BLACK                              (0)

static const uint32_t palette[] = { RED, GREEN, BLUE, YELLOW, VIOLET, TURQUOISE, WHITE, BLACK };
static const uint32_t erase_color = WHITE;
