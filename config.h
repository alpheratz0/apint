#define RED                                (0xff0000)
#define GREEN                              (0xff00)
#define BLUE                               (0xff)
#define YELLOW                             (RED|GREEN)
#define VIOLET                             (RED|BLUE)
#define TURQUOISE                          (GREEN|BLUE)
#define WHITE                              (RED|GREEN|BLUE)
#define BLACK                              (0)

#define START_COLOR                        (RED)
#define ERASE_COLOR                        (WHITE)

static const uint32_t palette[] = { RED, GREEN, BLUE, YELLOW, VIOLET, TURQUOISE, WHITE, BLACK };
