# My_OS
Simple Dictionary OS (fr) with bootloader

DictOS

Implementation of a simple dictionary-translator from English into the specified language.
Loader: interacts with the user, allowing the user to specify - words on which letters of the English language he will be interested after starting the OS. The loader prompts the user to allow words to be used on the specified letter or to prohibit using them by pressing the corresponding key once. In this case, the screen indicates which letters are marked and which are not:
abcde_____kl_nop____uv_x_z
When you press a key, for example 'w' the screen is updated:
abcde_____kl_nop____uvwx_z
To start the OS, the user presses Enter.

The dictionary can be stored in the kernel data section (you can declare it directly in the code as static data). The dictionary should be sorted. It is possible to store the dictionary in unsorted form, but the sorting is performed when the OS starts.
The dictionary must contain at least 50 English common words.
To search in the dictionary, binary search must be used.

Supported operating systems:

info
Displays information about the author and development tools of the OS (assembler compiler, compiler), the parameters specified in the bootloader - a list of letters, words to be processed when requesting a translation.

dictinfo
Retrieves information about the loaded dictionary (language, total number of words, the number of words available for translating - calculated based on those specified in the loader
data). Example:
# dictinfo
Dictionary: en -> es
Number of words: 1121
Number of loaded words: 780

translate word
Translates the word from English into another language. If a word is not found or not loaded, displays an error. Example:
# translate cat
gato
# translate airport
aeropuerto
# translate airport
aeropuerto

# translate moonlight
Error: word 'moonlight' is unchanged

wordstat letter
Displays the number of loaded words in the dictionary for the specified letter.
# wordstat z
Letter 'z': 57 words loaded.
# wordstat y
Letter 'z': 1 word loaded.
# wordstat d
Letter 'd': 0 words loaded.

shutdown
Shutdown the computer.
