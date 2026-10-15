/* TeXAspell spell service for macOS.
 *
 * A background program, installed in ~/Library/Services, that registers a
 * British English spell checker with the system. Any Cocoa application that
 * uses the system spell checker (TeXShop, TextEdit, ...) can then select it
 * in the Spelling and Grammar panel. All the work is done by the engine in
 * src/core; this file only translates between Cocoa and the engine.
 */
#import <AppKit/AppKit.h>

#include "../core/checker.h"

#define MAX_GUESSES 10

static NSString *const kLanguage = @"en_GB";
static NSString *const kVendor = @"TeXAspell";

@class TASpellDelegate;
/* NSSpellServer does not retain its delegate; keep it alive here. */
static TASpellDelegate *gDelegate;

@interface TASpellDelegate : NSObject <NSSpellServerDelegate>
- (instancetype)initWithEngine:(texaspell *)engine;
@end

@implementation TASpellDelegate {
    texaspell *_engine;
}

- (instancetype)initWithEngine:(texaspell *)engine
{
    if ((self = [super init]))
        _engine = engine;
    return self;
}

/* Check a string and return the ranges (in UTF-16 units, as NSString
 * counts) of everything flagged. */
- (NSArray<NSValue *> *)issueRangesInString:(NSString *)string
                                  wordCount:(NSInteger *)wordCount
{
    NSData *data = [string dataUsingEncoding:NSUTF8StringEncoding
                        allowLossyConversion:YES];
    const char *bytes = data.bytes;
    size_t len = data.length;
    ta_issue *issues = NULL;
    size_t nwords = 0, n;

    @synchronized(self) {
        n = ta_check(_engine, bytes ? bytes : "", len, &issues, &nwords);
    }
    if (wordCount)
        *wordCount = (NSInteger)nwords;
    if (n == 0) {
        free(issues);
        return @[];
    }

    size_t *map = malloc((len + 1) * sizeof *map);
    if (!map) {
        free(issues);
        return @[];
    }
    ta_utf16_map(bytes, len, map);
    NSMutableArray<NSValue *> *ranges = [NSMutableArray arrayWithCapacity:n];
    for (size_t i = 0; i < n; i++) {
        NSUInteger a = map[issues[i].start], b = map[issues[i].end];
        [ranges addObject:[NSValue valueWithRange:NSMakeRange(a, b - a)]];
    }
    free(map);
    free(issues);
    return ranges;
}

/* Older, one-at-a-time interface. */
- (NSRange)spellServer:(NSSpellServer *)sender
    findMisspelledWordInString:(NSString *)stringToCheck
                      language:(NSString *)language
                     wordCount:(NSInteger *)wordCount
                     countOnly:(BOOL)countOnly
{
    NSArray<NSValue *> *ranges = [self issueRangesInString:stringToCheck
                                                 wordCount:wordCount];
    if (countOnly || ranges.count == 0)
        return NSMakeRange(NSNotFound, 0);
    return ranges[0].rangeValue;
}

/* Unified checking interface used for check-as-you-type. */
- (NSArray<NSTextCheckingResult *> *)spellServer:(NSSpellServer *)sender
                                     checkString:(NSString *)stringToCheck
                                          offset:(NSUInteger)offset
                                           types:(NSTextCheckingTypes)checkingTypes
                                         options:(NSDictionary<NSString *, id> *)options
                                     orthography:(NSOrthography *)orthography
                                       wordCount:(NSInteger *)wordCount
{
    if (!(checkingTypes & NSTextCheckingTypeSpelling)) {
        if (wordCount)
            *wordCount = 0;
        return @[];
    }
    NSArray<NSValue *> *ranges = [self issueRangesInString:stringToCheck
                                                 wordCount:wordCount];
    NSMutableArray<NSTextCheckingResult *> *results =
        [NSMutableArray arrayWithCapacity:ranges.count];
    for (NSValue *v in ranges) {
        NSRange r = v.rangeValue;
        r.location += offset;
        [results addObject:[NSTextCheckingResult spellCheckingResultWithRange:r]];
    }
    return results;
}

- (NSArray<NSString *> *)spellServer:(NSSpellServer *)sender
               suggestGuessesForWord:(NSString *)word
                          inLanguage:(NSString *)language
{
    const char *w = word.UTF8String;
    if (!w)
        return @[];
    char *guesses[MAX_GUESSES];
    size_t n;
    @synchronized(self) {
        n = ta_suggest(_engine, w, strlen(w), guesses, MAX_GUESSES);
    }
    NSMutableArray<NSString *> *out = [NSMutableArray arrayWithCapacity:n];
    for (size_t i = 0; i < n; i++) {
        NSString *s = [NSString stringWithUTF8String:guesses[i]];
        if (s)
            [out addObject:s];
        free(guesses[i]);
    }
    return out;
}

- (void)spellServer:(NSSpellServer *)sender
       didLearnWord:(NSString *)word
         inLanguage:(NSString *)language
{
    const char *w = word.UTF8String;
    if (!w)
        return;
    @synchronized(self) {
        ta_learn(_engine, w, strlen(w));
    }
}

- (void)spellServer:(NSSpellServer *)sender
      didForgetWord:(NSString *)word
         inLanguage:(NSString *)language
{
    const char *w = word.UTF8String;
    if (!w)
        return;
    @synchronized(self) {
        ta_forget(_engine, w, strlen(w));
    }
}

@end

int main(void)
{
    @autoreleasepool {
        char err[1024];
        texaspell *engine = ta_open(NULL, err, sizeof err);
        if (!engine) {
            NSLog(@"TeXAspell: %s", err);
            return 1;
        }
        if (ta_warning(engine))
            NSLog(@"TeXAspell: %s", ta_warning(engine));
        NSLog(@"TeXAspell: dictionary %s, command checking %s, settings in %s",
              ta_dictionary(engine), ta_commands_enabled(engine) ? "on" : "off",
              ta_home(engine));

        NSSpellServer *server = [[NSSpellServer alloc] init];
        if (![server registerLanguage:kLanguage byVendor:kVendor]) {
            NSLog(@"TeXAspell: could not register with the system spell checker");
            ta_close(engine);
            return 1;
        }
        gDelegate = [[TASpellDelegate alloc] initWithEngine:engine];
        server.delegate = gDelegate;
        [server run];
        ta_close(engine);
    }
    return 0;
}
