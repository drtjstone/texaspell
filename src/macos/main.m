/* TeXAspell spell service for macOS.
 *
 * A background program, installed in ~/Library/Services, that registers
 * spell checkers with the system: British English, plus any other languages
 * listed in its Info.plist (set with make install LANGUAGES="..."). Any
 * Cocoa application that uses the system spell checker (TeXShop, TextEdit,
 * ...) can then select one in the Spelling and Grammar panel. All the work
 * is done by the engine in src/core; this file only translates between
 * Cocoa and the engine.
 */
#import <AppKit/AppKit.h>

#include "../core/checker.h"

#define MAX_GUESSES 10

static NSString *const kVendor = @"TeXAspell";

@class TASpellDelegate;
/* NSSpellServer does not retain its delegate; keep it alive here. */
static TASpellDelegate *gDelegate;

@interface TASpellDelegate : NSObject <NSSpellServerDelegate>
- (instancetype)initWithEngine:(texaspell *)engine
                     languages:(NSArray<NSString *> *)languages;
@end

@implementation TASpellDelegate {
    texaspell *_engine;
    NSArray<NSString *> *_languages; /* registered; the first is the default */
    NSString *_lastLogged;
}

- (instancetype)initWithEngine:(texaspell *)engine
                     languages:(NSArray<NSString *> *)languages
{
    if ((self = [super init])) {
        _engine = engine;
        _languages = [languages copy];
    }
    return self;
}

/* Map what macOS asks for ("en_GB", "en-GB", or just "en") to one of the
 * languages we registered. */
- (NSString *)resolve:(NSArray<NSString *> *)requested
{
    for (NSString *r in requested) {
        NSString *n = [r stringByReplacingOccurrencesOfString:@"-" withString:@"_"];
        if ([_languages containsObject:n])
            return n;
    }
    NSCharacterSet *seps = [NSCharacterSet characterSetWithCharactersInString:@"_-"];
    for (NSString *r in requested) {
        NSString *base = [r componentsSeparatedByCharactersInSet:seps].firstObject;
        NSString *prefix = [base stringByAppendingString:@"_"];
        for (NSString *l in _languages)
            if ([l isEqualToString:base] || [l hasPrefix:prefix])
                return l;
    }
    return _languages.firstObject;
}

/* Switch the engine to the language asked for. Call inside
 * @synchronized(self). */
- (BOOL)useLanguages:(NSArray<NSString *> *)requested
{
    NSString *language = [self resolve:requested];
    NSString *note = [NSString stringWithFormat:@"%@ -> %@",
                      [requested componentsJoinedByString:@","], language];
    if (![note isEqualToString:_lastLogged]) {
        NSLog(@"TeXAspell: asked for %@", note);
        _lastLogged = note;
    }
    char err[1024];
    if (ta_use_language(_engine, language.UTF8String, err, sizeof err))
        return YES;
    NSLog(@"TeXAspell: %s", err);
    return NO;
}

/* Check a string and return the ranges (in UTF-16 units, as NSString
 * counts) of everything flagged. */
- (NSArray<NSValue *> *)issueRangesInString:(NSString *)string
                                  languages:(NSArray<NSString *> *)languages
                                  wordCount:(NSInteger *)wordCount
                                     server:(NSSpellServer *)server
{
    NSData *data = [string dataUsingEncoding:NSUTF8StringEncoding
                        allowLossyConversion:YES];
    const char *bytes = data.bytes;
    size_t len = data.length;
    ta_issue *issues = NULL;
    size_t nwords = 0, n;

    if (wordCount)
        *wordCount = 0;
    @synchronized(self) {
        if (![self useLanguages:languages])
            return @[];
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
        NSString *word = [string substringWithRange:NSMakeRange(a, b - a)];
        if (server && [server isWordInUserDictionaries:word
                                        caseSensitive:issues[i].kind != TA_WORD])
            continue;
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
    NSArray<NSValue *> *ranges =
        [self issueRangesInString:stringToCheck
                        languages:language ? @[ language ] : @[]
                        wordCount:wordCount
                           server:sender];
    if (countOnly || ranges.count == 0)
        return NSMakeRange(NSNotFound, 0);
    return ranges[0].rangeValue;
}

/* Unified checking interface used for check-as-you-type. It does not name
 * the language chosen in the Spelling panel directly; that comes through
 * the orthography. */
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
    NSMutableArray<NSString *> *languages = [NSMutableArray array];
    if (orthography.dominantLanguage)
        [languages addObject:orthography.dominantLanguage];
    if (orthography.allLanguages)
        [languages addObjectsFromArray:orthography.allLanguages];
    NSArray<NSValue *> *ranges = [self issueRangesInString:stringToCheck
                                                 languages:languages
                                                 wordCount:wordCount
                                                    server:sender];
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
        if (![self useLanguages:language ? @[ language ] : @[]])
            return @[];
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

/* Learnt words are shared by all languages. */
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
        NSLog(@"TeXAspell: British dictionary %s, command checking %s, settings in %s",
              ta_dictionary(engine), ta_commands_enabled(engine) ? "on" : "off",
              ta_home(engine));
        char desc[4096];
        ta_describe(engine, desc, sizeof desc);
        NSLog(@"TeXAspell:\n%s", desc);

        /* Offer each language listed in Info.plist whose aspell dictionary
         * can be opened. */
        NSArray *services = [NSBundle mainBundle].infoDictionary[@"NSServices"];
        NSArray<NSString *> *languages = [services.firstObject objectForKey:@"NSLanguages"];
        if (languages.count == 0)
            languages = @[ @"en_GB" ];
        NSSpellServer *server = [[NSSpellServer alloc] init];
        NSMutableArray<NSString *> *registered = [NSMutableArray array];
        for (NSString *language in languages) {
            char lerr[1024];
            if (!ta_use_language(engine, language.UTF8String, lerr, sizeof lerr)) {
                NSLog(@"TeXAspell: not offering %@: %s", language, lerr);
                continue;
            }
            if ([server registerLanguage:language byVendor:kVendor]) {
                NSLog(@"TeXAspell: offering %@", language);
                [registered addObject:language];
            } else {
                NSLog(@"TeXAspell: could not register %@ with the system", language);
            }
        }
        if (registered.count == 0) {
            NSLog(@"TeXAspell: no languages registered");
            ta_close(engine);
            return 1;
        }
        ta_use_language(engine, NULL, err, sizeof err);

        gDelegate = [[TASpellDelegate alloc] initWithEngine:engine
                                                  languages:registered];
        server.delegate = gDelegate;
        [server run];
        ta_close(engine);
    }
    return 0;
}
