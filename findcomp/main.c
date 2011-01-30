#include <CoreFoundation/CoreFoundation.h>

#define AC_MIN_TOKEN_LENGTH 3
#define AC_MAX_LINE_LENGTH 2048

void writeText(CFStringRef text)
{
	// write to stdout
	//	CFStringEncoding encoding = CFStringGetFastestEncoding(text);
	CFStringEncoding encoding = kCFStringEncodingUTF8;
	CFIndex buf_len = CFStringGetMaximumSizeForEncoding(CFStringGetLength(text), encoding) + 1;
	
	char buffer[buf_len];
	if (CFStringGetCString(text, buffer, buf_len, encoding))
	{
		fprintf(stdout, "%s\n", buffer);
	}
}

void trace(CFStringRef text)
{
	writeText(text);
}

void processToken(CFStringRef text, CFStringRef token, CFRange token_range, CFIndex sel_start, CFMutableDictionaryRef word_list, CFStringRef *current_word)
{
	// determine current word
	if ((token_range.location < sel_start) && (token_range.location + token_range.length >= sel_start))
	{
		*current_word = CFStringCreateWithSubstring(NULL, text, CFRangeMake(token_range.location, sel_start-token_range.location));

//		trace(CFSTR("---- CURRENT WORD: ----"));
//		trace(*current_word);
//		trace(CFSTR("-----------------------"));
	}
	else
	{
		// add token to word list if it is not already defined
		if (CFStringGetLength(token) > AC_MIN_TOKEN_LENGTH)
		{
			CFDictionaryAddValue(word_list, token, NULL);
		}
	}
}

void tokenizeInput(CFStringRef text, CFIndex sel_start, CFMutableDictionaryRef word_list, CFStringRef *current_word)
{
	// tokenize document text by words
	CFRange range = CFRangeMake(0, CFStringGetLength(text));
	CFStringTokenizerRef tokenizer = CFStringTokenizerCreate(NULL, text, range, kCFStringTokenizerUnitWordBoundary, CFLocaleCopyCurrent());
	CFStringTokenizerTokenType token_type;
	
	while (0 != (token_type = CFStringTokenizerAdvanceToNextToken(tokenizer)))
	{
		CFRange token_range = CFStringTokenizerGetCurrentTokenRange(tokenizer);
		CFStringRef token = CFStringCreateWithSubstring(NULL, text, token_range);
		
		// check if token begins with a LETTER to be valid!
		CFIndex token_len = CFStringGetLength(token);
		CFRange valid_range;
		if (CFStringFindCharacterFromSet(token, CFCharacterSetGetPredefined(kCFCharacterSetLetter), CFRangeMake(0, token_len), kCFCompareAnchored, &valid_range))
		{
			CFArrayRef dot_occurences = CFStringCreateArrayWithFindResults(NULL, token, CFSTR("."), CFRangeMake(0, token_len), 0);
			if (NULL != dot_occurences)
			{
				// find tokens separated by dot
				CFIndex dot_count = CFArrayGetCount(dot_occurences);
				CFIndex dot_index = 0;
				CFIndex last_subtoken_start = 0;
				CFIndex subtoken_len = 0;
				CFStringRef subtoken = NULL;
				do
				{
					CFRange *dot_range = (CFRange *)CFArrayGetValueAtIndex(dot_occurences, dot_index);
					subtoken_len = dot_range->location;
					subtoken = CFStringCreateWithSubstring(NULL, token, CFRangeMake(last_subtoken_start, subtoken_len));
					
					processToken(text, subtoken, CFRangeMake(token_range.location + last_subtoken_start, subtoken_len), sel_start, word_list, current_word);
					
					CFRelease(subtoken);
					last_subtoken_start = dot_range->location + 1;
					dot_index++;
				}
				while(dot_index<dot_count);
				
				// add last subtoken
				subtoken_len = token_len - last_subtoken_start;
				subtoken = CFStringCreateWithSubstring(NULL, token, CFRangeMake(last_subtoken_start, subtoken_len));
				processToken(text, subtoken, CFRangeMake(token_range.location + last_subtoken_start, subtoken_len), sel_start, word_list, current_word);
				CFRelease(subtoken);
				
				CFRelease(dot_occurences);
			}
			else
			{
				// add complete token
				processToken(text, token, token_range, sel_start, word_list, current_word);
			}
		}
		
		CFRelease(token);
	}
	CFRelease(tokenizer);
}

void findCompletion(CFStringRef sel_text, CFMutableDictionaryRef word_list, CFStringRef *completion, CFStringRef current_word)
{
	// search for the next valid token
	CFIndex count = CFDictionaryGetCount(word_list);
	CFIndex index;
	CFTypeRef *keysTypeRef = (CFTypeRef *)malloc(count * sizeof(CFTypeRef));
	const void **keys = (const void **) keysTypeRef;
	CFDictionaryGetKeysAndValues(word_list, keys, NULL);
	CFStringRef first_token_found = nil;
	Boolean selection_found = false;
	for(index=0; index<count; index++)
	{
		CFStringRef token = keys[index];
		
		CFRange find_range = CFStringFind(token, current_word, kCFCompareAnchored);
		if (find_range.location != kCFNotFound)
		{
			// valid token
			CFIndex part_length = CFStringGetLength(token) - find_range.length;
			
			// ignore if current_word == token
			if (part_length<=0)
				continue;
			
			CFStringRef token_part = CFStringCreateWithSubstring(NULL, token, CFRangeMake(find_range.length, CFStringGetLength(token) - find_range.length));
			
			// set first find if empty
			if (nil == first_token_found)
			{
				first_token_found = CFStringCreateCopy(NULL, token_part);
				
				// if nothing is already selected, we are done
				if (nil == sel_text)
				{
					CFRelease(token_part);
					break;
				}
			}
			
			if (selection_found)
			{
				// take the next token
				*completion = CFStringCreateCopy(NULL, token_part);
				CFRelease(token_part);
				break;
			}
			
			if (kCFCompareEqualTo == CFStringCompare(token_part, sel_text, 0))
			{
				// sel_text token found! Take the next valid token as next completion.
				selection_found = true;
			}
			
			CFRelease(token_part);
		}
	}
	free(keys);
	
	if (nil != first_token_found)
	{
		if (nil == *completion)
		{
			*completion = CFStringCreateCopy(NULL, first_token_found);
		}
		
		CFRelease(first_token_found);
	}
}

int main (int argc, const char * argv[])
{
	// get current cursor pos from args (MANDATORY PARAMETER!)
	CFIndex sel_start = -1;
	if (argc>1)
	{
		sel_start = atoi(argv[1]) - 1;
	}

	if(sel_start<0)
	{
		fprintf(stderr, "USAGE: %s cursor_position [selection] < text\n", argv[0]);
		exit(1);
	}

	// get selected text part from args (OPTIONAL)
	CFStringRef sel_text = nil;
	if (argc>2)
	{
		sel_text = CFStringCreateWithCString(NULL, argv[2], kCFStringEncodingUTF8);
	}

	// init tokenize phase, fill word_list with tokens
	CFStringRef current_word = nil;
	CFMutableDictionaryRef word_list = CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, NULL);
	
	// TODO: read input linewise from stdin
	char input_line[AC_MAX_LINE_LENGTH+1];
	while(1)
	{
		if (!fgets(input_line, AC_MAX_LINE_LENGTH, stdin))
			break;
		
		CFStringRef text = CFStringCreateWithCString(kCFAllocatorDefault, input_line, kCFStringEncodingUTF8);
		
//		trace(CFSTR("---- NEW LINE ----"));
//		trace(text);
		
		tokenizeInput(text, sel_start, word_list, &current_word);
		sel_start -= (CFStringGetLength(text)); // NOTE: newlines are ignored (-1) if neccessary
		CFRelease(text);
		
	}
	
	// find suitable completion token
	CFStringRef completion = nil;
	if (nil != current_word)
	{
		findCompletion(sel_text, word_list, &completion, current_word);
		CFRelease(current_word);
	}
	
	// write new selection text
	if (nil != completion)
	{
		// write result to stdout
//		trace(CFSTR("---- COMPLETION: ----"));
    writeText(completion);
		CFRelease(completion);
	}

	
	// clean up
	if (nil != sel_text) CFRelease(sel_text);
	CFRelease(word_list);

	return 0;
}
