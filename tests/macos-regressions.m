/* Exercise both NSSpellServer callbacks without installing a service. */
#define main texaspell_service_entry_for_test
#import "../src/macos/main.m"
#undef main
@interface RegressionServer : NSSpellServer
@property(nonatomic, copy) NSSet<NSString *> *ignored;
@property(nonatomic) NSUInteger lookups;
@property(nonatomic) BOOL sawCaseSensitive;
@end
@implementation RegressionServer
- (BOOL)isWordInUserDictionaries:(NSString *)word caseSensitive:(BOOL)sensitive
{
    self.lookups++;
    if (sensitive) self.sawCaseSensitive = YES;
    return [self.ignored containsObject:word];
}
@end
static int failures;
static void require(BOOL good, const char *name)
{
    printf("%s    %s\n",good ? "ok" : "FAIL",name);
    if (!good) failures++;
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    @autoreleasepool {
        char err[1024];
        texaspell *ta=ta_open(argv[1],err,sizeof err);
        if (!ta) { fprintf(stderr,"%s\n",err); return 2; }
        TASpellDelegate *delegate=[[TASpellDelegate alloc] initWithEngine:ta languages:@[@"en_GB",@"en_US"]];
        RegressionServer *server=[RegressionServer new];
        server.ignored=[NSSet setWithObject:@"zzignoredword"];
        NSInteger count=0;
        NSString *text=@"zzignoredword mispellt";
        NSRange result=[delegate spellServer:server findMisspelledWordInString:text
                                   language:@"en_GB" wordCount:&count countOnly:NO];
        require(NSEqualRanges(result,[text rangeOfString:@"mispellt"]),
                "legacy callback advances past ignored first word");
        require(server.lookups>=2,"legacy callback consults system dictionary");
        NSOrthography *orth=[NSOrthography orthographyWithDominantScript:@"Latn"
                               languageMap:@{@"Latn":@[@"en_GB"]}];
        require([delegate respondsToSelector:@selector(spellServer:checkString:offset:types:options:orthography:wordCount:)],
                "unified callback retains system selector");
        NSArray<NSTextCheckingResult *> *results=[delegate spellServer:server checkString:text
                   offset:12 types:NSTextCheckingTypeSpelling options:@{} orthography:orth wordCount:&count];
        NSRange expected=[text rangeOfString:@"mispellt"];
        expected.location+=12;
        require(results.count==1 && NSEqualRanges(results[0].range,expected),
                "unified callback filters ignored word and applies offset");
        results=[delegate spellServer:server checkString:@"zzignoredword"
                   offset:0 types:NSTextCheckingTypeSpelling options:@{} orthography:orth wordCount:&count];
        require(results.count==0,"unified callback accepts entirely ignored input");
        server.ignored=[NSSet set];
        results=[delegate spellServer:server checkString:@"😀 good mispellt"
                   offset:12 types:NSTextCheckingTypeSpelling options:@{} orthography:orth wordCount:&count];
        require(results.count==1 && NSEqualRanges(results[0].range,NSMakeRange(20,8)),
                "unified callback converts emoji UTF-8 offsets to UTF-16");
        result=[delegate spellServer:server findMisspelledWordInString:@"\\sectoin"
                                   language:@"en_GB" wordCount:&count countOnly:NO];
        require(NSEqualRanges(result,NSMakeRange(0,8)),"command range includes backslash");
        require(server.sawCaseSensitive,"LaTeX dictionary lookup is case sensitive");
        NSArray *guesses=[delegate spellServer:server suggestGuessesForWord:@"\\sectoin" inLanguage:@"en_GB"];
        require([guesses containsObject:@"\\section"],"command replacement preserves backslash");
        [delegate spellServer:server didLearnWord:@"zzlearntword" inLanguage:@"en_GB"];
        result=[delegate spellServer:server findMisspelledWordInString:@"zzlearntword"
                                   language:@"en_GB" wordCount:&count countOnly:NO];
        require(result.location==NSNotFound,"native Learn Spelling callback");
        [delegate spellServer:server didForgetWord:@"zzlearntword" inLanguage:@"en_GB"];
        result=[delegate spellServer:server findMisspelledWordInString:@"zzlearntword"
                                   language:@"en_GB" wordCount:&count countOnly:NO];
        require(result.location!=NSNotFound,"native Forget Spelling callback");
        ta_close(ta);
    }
    return failures ? 1 : 0;
}
