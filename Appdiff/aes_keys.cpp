#include <cctype>
#include <cstdio>
#include <cstdlib>

#include "Core/Core.h"
#include "Unreal/UnCore.h"


static FString ParseKey(const char *RawKey);


#define ishex(c)		( (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') )
#define hextodigit(c)	( (c >= 'a') ? c - 'a' + 10 : c - '0' )


static void FromFile(TArray<FString> &AesKeys, const char *filename)
{
	guard(FromFile);

	FILE* f = fopen(filename, "r");
	if (!f)
	{
		appErrorNoLog("Unable to find aes key file \"%s\"", filename);
	}
  char *line = NULL;
  size_t len = 0;
  while ((getline(&line, &len, f)) != -1)
  {
    AesKeys.Add(ParseKey(line));
  }
  fclose(f);
  if (line) free(line);

	unguard;
  return;
}


static FString ParseKey(const char *RawKey)
{
  FString AesKey = RawKey;
	AesKey.TrimStartAndEndInline();

	if (AesKey.Len() < 3) return AesKey;

	const char* s = *AesKey;

	// Hex key starts with "0x"
	if (*s++ != '0') return AesKey;
	if (tolower(*s++) != 'x') return AesKey;

	FString NewKey;
	NewKey.Empty(AesKey.Len() / 2 + 1);

	int remains = AesKey.Len() - 2;
	if (remains & 1)
	{
		appErrorNoLog("Hexadecimal AES key contains odd number of characters");
	}
	while (remains > 0)
	{
		uint8 b = 0;
		if ((remains & 1) == 0)
		{
			// this code will not be executed only in a case of odd character count, for the first char
			char c = tolower(*s++);
			if (!ishex(c))
			{
				appErrorNoLog("Illegal character in hexadecimal AES key");
			}
			b = hextodigit(c) << 4;
			remains--;
		}
		char c = tolower(*s++);
		if (!ishex(c))
		{
			appErrorNoLog("Illegal character in hexadecimal AES key");
		}
		b |= hextodigit(c);
		remains--;

		NewKey.AppendChar((char)b);
	}

	return NewKey;
}


void ParseKeys(TArray<FString> &AesKeys, const char *arg)
{
  if (!arg) return;

	if (arg[0] == '@')
	{
    FromFile(AesKeys, arg+1);
	}
  else
  {
    AesKeys.Add(ParseKey(arg));
  }
  return;
}
