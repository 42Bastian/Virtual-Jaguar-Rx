//
// DWARFManager.cpp: DWARF format manager
//
// by Jean-Paul Mari
//
// JPM = Jean-Paul Mari <djipi.mari@gmail.com>
//  RG = Richard Goedeken
//
// WHO  WHEN        WHAT
// ---  ----------  ------------------------------------------------------------
// JPM   Dec./2016  Created this file, and added the DWARF format support
// JPM  Sept./2018  Added LEB128 decoding features, and improve the DWARF parsing information
// JPM   Oct./2018  Improve the DWARF parsing information, and the source file text reading; support the used source lines from DWARF structure, and the search paths for the files
// JPM   Aug./2019  Added new functions to handle DWARF information, full filename fix
// JPM   Mar./2020  Fix a random crash when reading the source lines information, and added a source code file date check
//  RG   Jan./2021  Linux build fixes
// JPM   Apr./2021  Support the structure and union members
// JPM   June/2021  Update the source file path clean up
// JPM   Oct./2021  Support wider offset ranges for local and parameter variables
// JPM  March/2022  Added a '/cygdrive/' directory detection
//

// To Do
// To use pointers instead of arrays usage (originally done to check if values are set at the right places)
// 


#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "libdwarf.h"
#include "dwarf.h"
#include "LEB128.h"
#include "DWARFManager.h"

// Definitions for debugging
//#define DEBUG_NumCU			0x2				// CU number to debug or undefine it
//#define DEBUG_VariableName	"cvar_vars"				// Variable name to look for or undefine it
//#define DEBUG_TypeName		"edict_t"			// Type name to look for or undefine it
//#define DEBUG_TypeDef			DW_TAG_typedef		// Type def to look for or undefine it (not used / not supported)
//#define DEBUG_Filename		"crt0"			// Filename to look for or undefine it

// Definitions for handling data
//#define CONVERT_QT_HML								// Text will be converted as HTML

// Definitions for the variables's typetag
#define	TypeTag_structure			0x01			// structure
#define	TypeTag_pointer				0x02			// pointer
#define	TypeTag_subrange			0x04			// (subrange_type?)
#define	TypeTag_arraytype			0x08			// array type
#define	TypeTag_consttype			0x10			// const type
#define	TypeTag_typedef				0x20			// typedef
#define TypeTag_enumeration_type	0x40			// enumeration
#define TypeTag_subroutine_type		0x80			// subroutine
#define TypeTag_union				0x100			// union


// Source line CU structure
typedef struct CUStruct_LineSrc
{
	size_t StartPC;
	size_t NumLineSrc;
	char *PtrLineSrc;
}S_CUStruct_LineSrc;

// Source line internal structure
typedef struct DMIStruct_LineSrc
{
	size_t Tag;
	size_t StartPC;
	size_t NumLineSrc;
	char *PtrLineSrc;
}S_DMIStruct_LineSrc;

// Enumeration structure
typedef struct EnumerationStruct
{
	char *PtrName;									// Enumeration's name
	size_t value;									// Enumeration's value
}S_EnumerationStruct;

// Structure members structure
typedef struct StructureMembersStruct
{
	char *PtrName;									// Structure member's name
	size_t TypeOffset;								// Structure member's offset on another type
	size_t DataMemberLocation;						// Structure member's data member
}S_StructureMembersStruct;

// Base type internal structure
typedef struct BaseTypeStruct
{
	size_t Tag;										// Type's Tag
	size_t Offset;									// Type's offset
	size_t TypeOffset;								// Type's offset on another type
	size_t ByteSize;								// Type's Byte Size
	size_t Encoding;								// Type's encoding
	char *PtrName;									// Type's name
	size_t NbEnumerations;							// Type's enumeration numbers
	EnumerationStruct *PtrEnumerations;				// Type's enumeration
	size_t NbStructureMembers;						// Type's numbers of structure members
	StructureMembersStruct *PtrStructureMembers;	// Type's structure members
}S_BaseTypeStruct;

// Variables internal structure
typedef struct VariablesStruct
{
	size_t Op;										// Variable's DW_OP
	union
	{
		size_t Addr;								// Variable memory address
		int Offset;									// Variable stack offset (signed)
	};
	char *PtrName;									// Variable's name
	size_t TypeOffset;								// Offset pointing on the Variable's Type
	size_t TypeByteSize;							// Variable's Type byte size
	size_t TypeTag;									// Variable's Type Tag
	size_t TypeEncoding;							// Variable's Type encoding
	char *PtrTypeName;								// Variable's Type name
	size_t NbTabVariables;							// Number of Variable's members
	VariablesStruct **TabVariables;					// Variable's Members (used for structures at the moment)
}S_VariablesStruct;

// Sub program internal structure
typedef struct SubProgStruct
{
	size_t Tag;
	size_t NumLineSrc;
	size_t StartPC;
	size_t LowPC, HighPC;
	size_t FrameBase;
	char *PtrLineSrc;
	char *PtrSubprogramName;						// Sub program name
	size_t NbLinesSrc;								// Number of lines source used by the sub program
	DMIStruct_LineSrc *PtrLinesSrc;					// Pointer of the lines source for the sub program
	size_t NbVariables;								// Variables number
	VariablesStruct *PtrVariables;					// Pointer to the local variables list information structure
}S_SubProgStruct;

// Compilation Unit internal structure
typedef struct CUStruct
{
	size_t Tag;
	size_t Language;								// Language (C, etc.) used by the source code
	size_t LowPC, HighPC;							// Memory range for the code
	char *PtrProducer;								// "Producer" text information (mostly compiler and compilation options used)
	char *PtrSourceFilename;						// Source file name
	char *PtrSourceFileDirectory;					// Directory of the source file
	char *PtrFullFilename;							// Pointer to full namefile (directory & filename)
	size_t SizeLoadSrc;								// Source code text size
	char *PtrLoadSrc;								// Pointer to the source code text
	size_t NbLinesLoadSrc;							// Total number of lines in the source code text
	char **PtrLinesLoadSrc;							// Pointer lists to each source line put in QT html/text conformity
	size_t NbSubProgs;								// Number of sub programs / routines
	SubProgStruct *PtrSubProgs;						// Pointer to the sub programs / routines structure
	size_t NbTypes;									// Number of types
	BaseTypeStruct *PtrTypes;						// Pointer to types
	size_t NbVariables;								// Variables number
	VariablesStruct *PtrVariables;					// Pointer to the global variables list structure
	size_t NbFrames;								// Frames number
	size_t NbUsedLinesSrc;							// Number of used source lines
	size_t LastNumUsedLinesSrc;						// Last number line used
	CUStruct_LineSrc *PtrUsedLinesSrc;				// Pointer to the used source lines list structure
	char **PtrUsedLinesLoadSrc;						// Pointer lists to each used source line referenced by the CUStruct_LineSrc structure
	size_t *PtrUsedNumLines;						// List of the number lines used
	struct stat _statbuf;							// File information
	DWARFstatus Status;								// File status
}S_CUStruct;


// Dwarf management
uint32_t LibDwarf;
uint32_t NbCU;
Dwarf_Ptr errarg;
Dwarf_Error error;
Dwarf_Debug dbg;
CUStruct *PtrCU;
char **ListSearchPaths;
size_t NbSearchPaths;
struct stat FileElfExeInfo;


// Function declarations
Dwarf_Handler DWARFManager_ErrorHandler(Dwarf_Ptr perrarg);
void DWARFManager_InitDMI(void);
void DWARFManager_CloseDMI(void);
bool DWARFManager_ElfClose(void);
char *DWARFManager_GetLineSrcFromNumLine(char *PtrSrcFile, size_t NumLine);
void DWARFManager_InitInfosVariable(VariablesStruct *PtrVariables);
void DWARFManager_SourceFileSearchPathsInit(void);
void DWARFManager_SourceFileSearchPathsReset(void);
void DWARFManager_SourceFileSearchPathsClose(void);
void DWARFManager_ConformSlachesBackslashes(char *Ptr);
#if 0
size_t DWARFManager_GetNbGlobalVariables(void);
size_t DWARFManager_GetNbLocalVariables(size_t Adr);
#endif


//
Dwarf_Handler DWARFManager_ErrorHandler(Dwarf_Ptr /* perrarg */)
{
	return	0;
}


// Dwarf manager list search paths init
void DWARFManager_SourceFileSearchPathsInit(void)
{
	ListSearchPaths = NULL;
	NbSearchPaths = 0;
}


// Dwarf manager list search paths reset
void DWARFManager_SourceFileSearchPathsReset(void)
{
	ListSearchPaths = NULL;
	NbSearchPaths = 0;
}


// Dwarf manager list search paths close
void DWARFManager_SourceFileSearchPathsClose(void)
{
	DWARFManager_SourceFileSearchPathsReset();
}


// Dwarf manager init
void DWARFManager_Init(void)
{
	DWARFManager_SourceFileSearchPathsInit();
	LibDwarf = DW_DLV_NO_ENTRY;
}


// Dwarf manager settings
void DWARFManager_Set(size_t NbPathsInList, char **PtrListPaths)
{
	// Search paths init
	ListSearchPaths = PtrListPaths;
	NbSearchPaths = NbPathsInList;
}


// Dwarf manager Reset
bool DWARFManager_Reset(void)
{
	DWARFManager_SourceFileSearchPathsReset();
	return DWARFManager_ElfClose();
}


// Dwarf manager Close
bool DWARFManager_Close(void)
{
	DWARFManager_SourceFileSearchPathsClose();
	return(DWARFManager_Reset());
}


// Dwarf manager Elf init
int	DWARFManager_ElfInit(Elf *ElfPtr, struct stat FileElfInfo)
{
	if ((LibDwarf = dwarf_elf_init(ElfPtr, DW_DLC_READ, (Dwarf_Handler)DWARFManager_ErrorHandler, errarg, &dbg, &error)) == DW_DLV_OK)
	{
		FileElfExeInfo = FileElfInfo;
		DWARFManager_InitDMI();
	}

	return LibDwarf;
}


// Dwarf manager Elf close
bool DWARFManager_ElfClose(void)
{
	if (LibDwarf == DW_DLV_OK)
	{
		DWARFManager_CloseDMI();

		if (dwarf_finish(dbg, &error) == DW_DLV_OK)
		{
			LibDwarf = DW_DLV_NO_ENTRY;
			return	true;
		}
		else
		{
			return	false;
		}
	}
	else
	{
		return	true;
	}
}


// Dwarf manager Compilation Units close
void DWARFManager_CloseDMI(void)
{
	// loop on all CU
	while (NbCU--)
	{
		// free pointers
		free(PtrCU[NbCU].PtrFullFilename);
		free(PtrCU[NbCU].PtrLoadSrc);
		free(PtrCU[NbCU].PtrProducer);
		free(PtrCU[NbCU].PtrSourceFilename);
		free(PtrCU[NbCU].PtrSourceFileDirectory);
		free(PtrCU[NbCU].PtrUsedLinesSrc);
		free(PtrCU[NbCU].PtrUsedLinesLoadSrc);
		free(PtrCU[NbCU].PtrUsedNumLines);

		// free lines from the source code
		while (PtrCU[NbCU].NbLinesLoadSrc--)
		{
			free(PtrCU[NbCU].PtrLinesLoadSrc[PtrCU[NbCU].NbLinesLoadSrc]);
		}
		free(PtrCU[NbCU].PtrLinesLoadSrc);

		// free the functions information
		while (PtrCU[NbCU].NbSubProgs--)
		{
			while (PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables--)
			{
				free(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].PtrName);
				free(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].PtrTypeName);
			}
			free(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables);

			free(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc);
			free(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrSubprogramName);
		}
		free(PtrCU[NbCU].PtrSubProgs);

		// free the types
		while (PtrCU[NbCU].NbTypes--)
		{
			free(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrName);

			// free the structure's members
			while (PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers--)
			{
				free(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].PtrName);
			}
			free(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers);
		}
		free(PtrCU[NbCU].PtrTypes);

		// free variables
		while (PtrCU[NbCU].NbVariables--)
		{
			free(PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName);
			free(PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrTypeName);

			// free the variable's members
			while (PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].NbTabVariables--)
			{
				free(PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].TabVariables[PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].NbTabVariables]);
			}
		}
		free(PtrCU[NbCU].PtrVariables);
	}

	// free the CU
	free(PtrCU);
}


// Dwarf manager Compilation Units initialisations
void DWARFManager_InitDMI(void)
{
	Dwarf_Unsigned	next_cu_header, return_uvalue;
	Dwarf_Error	error;
	Dwarf_Attribute	*atlist;
	Dwarf_Attribute	return_attr1;
	Dwarf_Half return_tagval, return_attr;
	Dwarf_Half version, offset_size;
	Dwarf_Addr return_lowpc, return_highpc, return_lineaddr;
	Dwarf_Block *return_block;
	Dwarf_Signed atcnt, cnt, return_value;
	Dwarf_Die return_sib, return_die, return_sub, return_subdie;
	Dwarf_Off return_offset;
	Dwarf_Line *linebuf;
	Dwarf_Half form;
	FILE *SrcFile;
	char *return_string;
	char *Ptr, *Ptr1;

	// Initialisation for the Compilation Units table
	NbCU = 0;
	PtrCU = NULL;

	// loop on the available Compilation Unit
	while (dwarf_next_cu_header_b(dbg, NULL, &version, NULL, NULL, &offset_size, NULL, &next_cu_header, &error) == DW_DLV_OK)
	{
		// Allocation of an additional Compilation Unit structure in the table
		if (Ptr = (char *)realloc(PtrCU, ((NbCU + 1) * sizeof(CUStruct))))
		{
			// Compilation Unit RAZ
			PtrCU = (CUStruct *)Ptr;
			memset(PtrCU + NbCU, 0, sizeof(CUStruct));

			// Debug specific CU
#ifdef DEBUG_NumCU
			if (NbCU == DEBUG_NumCU)
#endif
			{
				// Get 1st Die from the Compilation Unit
				if (dwarf_siblingof(dbg, NULL, &return_sib, &error) == DW_DLV_OK)
				{
					// Get Die's Tag
					if ((dwarf_tag(return_sib, &return_tagval, &error) == DW_DLV_OK))
					{
						PtrCU[NbCU].Tag = return_tagval;

						// Die type detection
						switch (return_tagval)
						{
						case DW_TAG_compile_unit:
							if (dwarf_attrlist(return_sib, &atlist, &atcnt, &error) == DW_DLV_OK)
							{
								for (Dwarf_Signed i = 0; i < atcnt; ++i)
								{
									if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
									{
										switch (return_attr)
										{
											// Start address
										case DW_AT_low_pc:
											if (dwarf_lowpc(return_sib, &return_lowpc, &error) == DW_DLV_OK)
											{
												PtrCU[NbCU].LowPC = return_lowpc;
											}
											break;

											// End address
										case DW_AT_high_pc:
											if (dwarf_highpc(return_sib, &return_highpc, &error) == DW_DLV_OK)
											{
												PtrCU[NbCU].HighPC = return_highpc;
											}
											break;

											// compilation information
										case DW_AT_producer:
											if (dwarf_formstring(atlist[i], &return_string, &error) == DW_DLV_OK)
											{
												PtrCU[NbCU].PtrProducer = (char *)calloc(strlen(return_string) + 1, 1);
												strcpy(PtrCU[NbCU].PtrProducer, return_string);
												dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
											}
											break;

											// Filename
										case DW_AT_name:
											if (dwarf_formstring(atlist[i], &return_string, &error) == DW_DLV_OK)
											{
#ifdef DEBUG_Filename
												if (strstr(return_string, DEBUG_Filename))
#endif
												{
													PtrCU[NbCU].PtrSourceFilename = (char *)calloc((strlen(return_string) + 1), 1);
													strcpy(PtrCU[NbCU].PtrSourceFilename, return_string);
												}
												dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
											}
											break;

											// Directory name
										case DW_AT_comp_dir:
											if (dwarf_formstring(atlist[i], &return_string, &error) == DW_DLV_OK)
											{
												PtrCU[NbCU].PtrSourceFileDirectory = (char *)calloc((strlen(return_string) + 1), 1);
												strcpy(PtrCU[NbCU].PtrSourceFileDirectory, return_string);
												dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
											}
											break;

											// Language
										case DW_AT_language:
											if (dwarf_formudata(atlist[i], &return_uvalue, &error) == DW_DLV_OK)
											{
												PtrCU[NbCU].Language = return_uvalue;
											}
											break;

										default:
											break;
										}
									}
									dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
								}
								dwarf_dealloc(dbg, atlist, DW_DLA_LIST);
							}

							// check filename presence
							if (!PtrCU[NbCU].PtrSourceFilename)
							{
								PtrCU[NbCU].PtrSourceFilename = (char *)calloc(1, 1);
							}

							// check directory presence
							if (!PtrCU[NbCU].PtrSourceFileDirectory)
							{
								// check if file exists in the search paths
								for (size_t i = 0; i < NbSearchPaths; i++)
								{
									PtrCU[NbCU].PtrFullFilename = (char *)realloc(PtrCU[NbCU].PtrFullFilename, strlen(PtrCU[NbCU].PtrSourceFilename) + strlen((const char *)ListSearchPaths[i]) + 2);
#if defined(_WIN32)
									sprintf(PtrCU[NbCU].PtrFullFilename, "%s\\%s", ListSearchPaths[i], PtrCU[NbCU].PtrSourceFilename);
									if (!fopen_s(&SrcFile, PtrCU[NbCU].PtrFullFilename, "rb"))
#else
									sprintf(PtrCU[NbCU].PtrFullFilename, "%s/%s", ListSearchPaths[i], PtrCU[NbCU].PtrSourceFilename);
									SrcFile = fopen(PtrCU[NbCU].PtrFullFilename, "rb");
									if (SrcFile == NULL)
#endif
									{
										PtrCU[NbCU].PtrSourceFileDirectory = (char *)realloc(PtrCU[NbCU].PtrSourceFileDirectory, strlen(ListSearchPaths[i]) + 1);
										strcpy(PtrCU[NbCU].PtrSourceFileDirectory, ListSearchPaths[i]);
									}
								}

								// file directory doesn't exits
								if (!PtrCU[NbCU].PtrSourceFileDirectory)
								{
									PtrCU[NbCU].PtrSourceFileDirectory = (char *)realloc(PtrCU[NbCU].PtrSourceFileDirectory, 2);
									strcpy(PtrCU[NbCU].PtrSourceFileDirectory, ".");
								}
							}
							else
							{
								// handle the /cygdrive/ presence in the directory filename
								if (!strncmp(PtrCU[NbCU].PtrSourceFileDirectory, "/cygdrive/", 10))
								{
									strcpy(PtrCU[NbCU].PtrSourceFileDirectory, &PtrCU[NbCU].PtrSourceFileDirectory[9]);
									if ((PtrCU[NbCU].PtrSourceFileDirectory[0] == '/') && (PtrCU[NbCU].PtrSourceFileDirectory[2] == '/') && (PtrCU[NbCU].PtrSourceFileDirectory[1] >= 'a') && (PtrCU[NbCU].PtrSourceFileDirectory[1] <= 'z'))
									{
										PtrCU[NbCU].PtrSourceFileDirectory[0] = PtrCU[NbCU].PtrSourceFileDirectory[1];
										PtrCU[NbCU].PtrSourceFileDirectory[1] = ':';
										PtrCU[NbCU].PtrSourceFileDirectory = (char*)realloc(PtrCU[NbCU].PtrSourceFileDirectory, strlen(PtrCU[NbCU].PtrSourceFileDirectory) + 1);
									}
								}
							}

							// conform slashes / backslashes for the filename
							DWARFManager_ConformSlachesBackslashes(PtrCU[NbCU].PtrSourceFilename);

							// check if filename contains already the complete directory
							if (PtrCU[NbCU].PtrSourceFilename[1] == ':')
							{
								// copy the filename as the full filename
								PtrCU[NbCU].PtrFullFilename = (char *)realloc(PtrCU[NbCU].PtrFullFilename, strlen(PtrCU[NbCU].PtrSourceFilename) + 1);
								strcpy(PtrCU[NbCU].PtrFullFilename, PtrCU[NbCU].PtrSourceFilename);
							}
							else
							{
								// create full filename and Conform slashes / backslashes
								PtrCU[NbCU].PtrFullFilename = (char *)realloc(PtrCU[NbCU].PtrFullFilename, strlen(PtrCU[NbCU].PtrSourceFilename) + strlen(PtrCU[NbCU].PtrSourceFileDirectory) + 2);
#if defined(_WIN32)
								sprintf(PtrCU[NbCU].PtrFullFilename, "%s\\%s", PtrCU[NbCU].PtrSourceFileDirectory, PtrCU[NbCU].PtrSourceFilename);
#else
								sprintf(PtrCU[NbCU].PtrFullFilename, "%s/%s", PtrCU[NbCU].PtrSourceFileDirectory, PtrCU[NbCU].PtrSourceFilename);
#endif
							}

							// directory path clean-up
							DWARFManager_ConformSlachesBackslashes(PtrCU[NbCU].PtrFullFilename);
#if defined(_WIN32)
							while ((Ptr = strstr(PtrCU[NbCU].PtrFullFilename, "\\..\\")) || (Ptr = strstr(PtrCU[NbCU].PtrFullFilename, "\\.\\")) || (Ptr = strstr(PtrCU[NbCU].PtrFullFilename, "\\\\")))
#else
							while ((Ptr = strstr(PtrCU[NbCU].PtrFullFilename, "/../")))
#endif
							{
								Ptr1 = Ptr;
#if defined(_WIN32)
								while ((*--Ptr1 != '\\') && (*Ptr1 != ':'));
								strcpy((Ptr1 + 1), (Ptr + (strstr(Ptr, "\\.\\") ? 2 : (strstr(Ptr, "\\\\") ? 1 : 4))));
#else
								while (*--Ptr1 != '/');
								strcpy((Ptr1 + 1), (Ptr + 4));
#endif
							}

							// get the source file information
							if (!stat(PtrCU[NbCU].PtrFullFilename, &PtrCU[NbCU]._statbuf))
							{
								// check the time stamp with the executable
								if (PtrCU[NbCU]._statbuf.st_mtime <= FileElfExeInfo.st_mtime)
								{
									// open the source file as a binary file
#if defined(_WIN32)
									if (!fopen_s(&SrcFile, PtrCU[NbCU].PtrFullFilename, "rb"))
#else
                                    SrcFile = fopen(PtrCU[NbCU].PtrFullFilename, "rb");
									if (SrcFile == NULL)
#endif
									{
										if (!fseek(SrcFile, 0, SEEK_END))
										{
											if ((PtrCU[NbCU].SizeLoadSrc = ftell(SrcFile)) > 0)
											{
												if (!fseek(SrcFile, 0, SEEK_SET))
												{
													if (PtrCU[NbCU].PtrLoadSrc = Ptr = Ptr1 = (char *)calloc(1, (PtrCU[NbCU].SizeLoadSrc + 2)))
													{
														// read whole file
#if defined(_WIN32)	&& defined(_MSC_VER)													
														if (fread_s(PtrCU[NbCU].PtrLoadSrc, PtrCU[NbCU].SizeLoadSrc, PtrCU[NbCU].SizeLoadSrc, 1, SrcFile) != 1)
#else
														if (fread(PtrCU[NbCU].PtrLoadSrc, PtrCU[NbCU].SizeLoadSrc, 1, SrcFile) != 1)
#endif
														{
															free(PtrCU[NbCU].PtrLoadSrc);
															PtrCU[NbCU].PtrLoadSrc = NULL;
															PtrCU[NbCU].SizeLoadSrc = 0;
														}
														else
														{
															// Eliminate all carriage return code '\r' (oxd)
															do
															{
																if ((*Ptr = *Ptr1) != '\r')
																{
																	Ptr++;
																}
															} while (*Ptr1++);

															// Get back the new text file size
															PtrCU[NbCU].SizeLoadSrc = strlen(Ptr = PtrCU[NbCU].PtrLoadSrc);

															// Make sure the text file finish with a new line code '\n' (0xa)
															if (PtrCU[NbCU].PtrLoadSrc[PtrCU[NbCU].SizeLoadSrc - 1] != '\n')
															{
																PtrCU[NbCU].PtrLoadSrc[PtrCU[NbCU].SizeLoadSrc++] = '\n';
																PtrCU[NbCU].PtrLoadSrc[PtrCU[NbCU].SizeLoadSrc] = 0;
															}

															// Reallocate text file
															if (PtrCU[NbCU].PtrLoadSrc = Ptr = (char *)realloc(PtrCU[NbCU].PtrLoadSrc, (PtrCU[NbCU].SizeLoadSrc + 1)))
															{
																// Count line numbers, based on the new line code '\n' (0xa), and finish each line with 0
																do
																{
																	if (*Ptr == '\n')
																	{
																		PtrCU[NbCU].NbLinesLoadSrc++;
																		*Ptr = 0;
																	}
																} while (*++Ptr);
															}
														}
													}
												}
											}
										}

										fclose(SrcFile);
									}
									else
									{
										// Source file doesn't exist
										PtrCU[NbCU].Status = DWARFSTATUS_NOFILE;
									}
								}
								else
								{
									// Source file is outdated
									PtrCU[NbCU].Status = DWARFSTATUS_OUTDATEDFILE;
								}
							}
							else
							{
								// Source file doesn't have information
								PtrCU[NbCU].Status = DWARFSTATUS_NOFILEINFO;
							}
							break;

						default:
							break;
						}
					}

					// Get the source lines table located in the CU
					if ((dwarf_srclines(return_sib, &linebuf, &cnt, &error) == DW_DLV_OK) && (PtrCU[NbCU].Status == DWARFSTATUS_OK))
					{
						if (cnt)
						{
							PtrCU[NbCU].NbUsedLinesSrc = cnt;
							PtrCU[NbCU].PtrUsedLinesSrc = (CUStruct_LineSrc *)calloc(cnt, sizeof(CUStruct_LineSrc));
							PtrCU[NbCU].PtrUsedLinesLoadSrc = (char **)calloc(cnt, sizeof(char *));
							PtrCU[NbCU].PtrUsedNumLines = (size_t *)calloc(cnt, sizeof(size_t));

							// Get the addresses and their source line numbers
							for (Dwarf_Signed i = 0; i < cnt; i++)
							{
								if (dwarf_lineaddr(linebuf[i], &return_lineaddr, &error) == DW_DLV_OK)
								{
									// Get the source line number
									if (dwarf_lineno(linebuf[i], &return_uvalue, &error) == DW_DLV_OK)
									{
										PtrCU[NbCU].PtrUsedLinesSrc[i].StartPC = return_lineaddr;
										PtrCU[NbCU].PtrUsedLinesSrc[i].NumLineSrc = return_uvalue;
									}
								}
							}
						}

						// Release the memory used by the source lines table located in the CU
						dwarf_srclines_dealloc(dbg, linebuf, cnt);
					}

					// Check if the CU has child
					if (dwarf_child(return_sib, &return_die, &error) == DW_DLV_OK)
					{
						do
						{
							return_sib = return_die;
							if ((dwarf_tag(return_die, &return_tagval, &error) == DW_DLV_OK))
							{
								switch (return_tagval)
								{
								case DW_TAG_lexical_block:
									break;

								case DW_TAG_variable:
									if (dwarf_attrlist(return_die, &atlist, &atcnt, &error) == DW_DLV_OK)
									{
										PtrCU[NbCU].PtrVariables = (VariablesStruct *)realloc(PtrCU[NbCU].PtrVariables, ((PtrCU[NbCU].NbVariables + 1) * sizeof(VariablesStruct)));
										memset(PtrCU[NbCU].PtrVariables + PtrCU[NbCU].NbVariables, 0, sizeof(VariablesStruct));

										for (Dwarf_Signed i = 0; i < atcnt; ++i)
										{
											if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
											{
												if (dwarf_attr(return_die, return_attr, &return_attr1, &error) == DW_DLV_OK)
												{
													switch (return_attr)
													{
													case DW_AT_location:
														if (dwarf_formblock(return_attr1, &return_block, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].Op = (*((unsigned char *)(return_block->bl_data)));

															switch (return_block->bl_len)
															{
															case 5:
																PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].Addr = (*((unsigned char *)(return_block->bl_data) + 1) << 24) + (*((unsigned char *)(return_block->bl_data) + 2) << 16) + (*((unsigned char *)(return_block->bl_data) + 3) << 8) + (*((unsigned char *)(return_block->bl_data) + 4));
																break;

															default:
																break;
															}
															dwarf_dealloc(dbg, return_block, DW_DLA_BLOCK);
														}
														break;

													case DW_AT_type:
														if (dwarf_global_formref(return_attr1, &return_offset, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].TypeOffset = return_offset;
														}
														break;

														// Variable name
													case DW_AT_name:
														if (dwarf_formstring(return_attr1, &return_string, &error) == DW_DLV_OK)
														{
#ifdef DEBUG_VariableName
															if (!strcmp(return_string, DEBUG_VariableName))
#endif
															{
																PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName = (char *)calloc(strlen(return_string) + 1, 1);
																strcpy(PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName, return_string);
															}
															dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
														}
														break;

														default:
														break;
													}
												}
											}

											dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
										}

										// Check variable's name validity
										if (PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName)
										{
											// Check variable's memory address validity
											if (PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].Addr)
											{
												// Valid variable
												PtrCU[NbCU].NbVariables++;
											}
											else
											{
												// Invalid variable
												free(PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName);
												PtrCU[NbCU].PtrVariables[PtrCU[NbCU].NbVariables].PtrName = NULL;
											}
										}

										dwarf_dealloc(dbg, atlist, DW_DLA_LIST);
									}
									break;

								case DW_TAG_base_type:
								case DW_TAG_typedef:
								case DW_TAG_union_type:
								case DW_TAG_structure_type:
								case DW_TAG_pointer_type:
								case DW_TAG_const_type:
								case DW_TAG_array_type:
								case DW_TAG_subrange_type:
								case DW_TAG_subroutine_type:
								case DW_TAG_enumeration_type:
									if (dwarf_attrlist(return_die, &atlist, &atcnt, &error) == DW_DLV_OK)
									{
										// Allocate memory for this type
										PtrCU[NbCU].PtrTypes = (BaseTypeStruct *)realloc(PtrCU[NbCU].PtrTypes, ((PtrCU[NbCU].NbTypes + 1) * sizeof(BaseTypeStruct)));
										memset(PtrCU[NbCU].PtrTypes + PtrCU[NbCU].NbTypes, 0, sizeof(BaseTypeStruct));
										PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].Tag = return_tagval;

										if (dwarf_dieoffset(return_die, &return_offset, &error) == DW_DLV_OK)
										{
											PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].Offset = return_offset;
										}

										for (Dwarf_Signed i = 0; i < atcnt; ++i)
										{
											if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
											{
												if (dwarf_attr(return_die, return_attr, &return_attr1, &error) == DW_DLV_OK)
												{
													switch (return_attr)
													{
														// 
													case DW_AT_sibling:
														break;

														// Type's type offset
													case DW_AT_type:
														if (dwarf_global_formref(return_attr1, &return_offset, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].TypeOffset = return_offset;
														}
														break;

														// Type's byte size
													case DW_AT_byte_size:
														if (dwarf_formudata(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].ByteSize = return_uvalue;
														}
														break;

														// Type's encoding
													case DW_AT_encoding:
														if (dwarf_formudata(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].Encoding = return_uvalue;
														}
														break;

														// Type's name
													case DW_AT_name:
														if (dwarf_formstring(return_attr1, &return_string, &error) == DW_DLV_OK)
														{
#ifdef DEBUG_TypeName
															if (!strcmp(return_string, DEBUG_TypeName))
#endif
															{
																PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrName = (char *)calloc(strlen(return_string) + 1, 1);
																strcpy(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrName, return_string);
															}
															dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
														}
														break;

														// Type's file number
													case DW_AT_decl_file:
														break;

														// Type's line number
													case DW_AT_decl_line:
														break;

													default:
														break;
													}
												}
											}

											dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
										}

										dwarf_dealloc(dbg, atlist, DW_DLA_LIST);

										switch (return_tagval)
										{
										case DW_TAG_structure_type:
										case DW_TAG_union_type:
											if (dwarf_child(return_die, &return_subdie, &error) == DW_DLV_OK)
											{
												do
												{
													return_sub = return_subdie;
													if ((dwarf_tag(return_subdie, &return_tagval, &error) == DW_DLV_OK))
													{
														switch (return_tagval)
														{
														case DW_TAG_member:
															if (dwarf_attrlist(return_subdie, &atlist, &atcnt, &error) == DW_DLV_OK)
															{
																// Allocate memory for this member
																PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers = (StructureMembersStruct *)realloc(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers, ((PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers + 1) * sizeof(StructureMembersStruct)));
																memset(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers + PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers, 0, sizeof(StructureMembersStruct));

																for (Dwarf_Signed i = 0; i < atcnt; ++i)
																{
																	if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
																	{
																		if (dwarf_attr(return_subdie, return_attr, &return_attr1, &error) == DW_DLV_OK)
																		{
																			switch (return_attr)
																			{
																			case DW_AT_data_member_location:
																				if (dwarf_whatform(return_attr1, &form, &error) == DW_DLV_OK)
																				{
																					if ((form == DW_FORM_data1) || (form == DW_FORM_data2) || (form == DW_FORM_data2) || (form == DW_FORM_data4) || (form == DW_FORM_data8) || (form == DW_FORM_udata))
																					{
																						if (dwarf_formudata(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
																						{
																							PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].DataMemberLocation = return_uvalue;
																						}
																					}
																					else
																					{
																						if (form == DW_FORM_sdata)
																						{
																							if (dwarf_formsdata(return_attr1, &return_value, &error) == DW_DLV_OK)
																							{
																								PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].DataMemberLocation = return_value;
																							}
																						}
																						else
																						{
																							if (dwarf_formblock(return_attr1, &return_block, &error) == DW_DLV_OK)
																							{
																								switch (return_block->bl_len)
																								{
																								case 2:
																								case 3:
																								case 4:
																									PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].DataMemberLocation = ReadULEB128((char *)return_block->bl_data + 1);
																									break;

																								default:
																									break;
																								}

																								dwarf_dealloc(dbg, return_block, DW_DLA_BLOCK);
																							}
																						}
																					}
																				}
																				break;

																			case DW_AT_type:
																				//dwarf_whatform(return_attr1, &form, &error);
																				if (dwarf_global_formref(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
																				{
																					PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].TypeOffset = return_uvalue;
																				}
																				break;

																			case DW_AT_name:
																				if (dwarf_formstring(return_attr1, &return_string, &error) == DW_DLV_OK)
																				{
																					PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].PtrName = (char *)calloc(strlen(return_string) + 1, 1);
																					strcpy(PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].PtrStructureMembers[PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers].PtrName, return_string);

																					dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
																				}
																				break;

																				// Member's file number
																			case DW_AT_decl_file:
																				break;

																				// Member's line number
																			case DW_AT_decl_line:
																				break;

																			default:
																				break;
																			}
																		}
																	}
																}
																dwarf_dealloc(dbg, atlist, DW_DLA_LIST);

																PtrCU[NbCU].PtrTypes[PtrCU[NbCU].NbTypes].NbStructureMembers++;
															}
															break;
														}
													}
												} while (dwarf_siblingof(dbg, return_sub, &return_subdie, &error) == DW_DLV_OK);
											}
											break;
										}

										PtrCU[NbCU].NbTypes++;
									}
									break;

								case DW_TAG_subprogram:
									if (dwarf_attrlist(return_die, &atlist, &atcnt, &error) == DW_DLV_OK)
									{
										PtrCU[NbCU].PtrSubProgs = (SubProgStruct *)realloc(PtrCU[NbCU].PtrSubProgs, ((PtrCU[NbCU].NbSubProgs + 1) * sizeof(SubProgStruct)));
										memset((void *)(PtrCU[NbCU].PtrSubProgs + PtrCU[NbCU].NbSubProgs), 0, sizeof(SubProgStruct));
										PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].Tag = return_tagval;

										for (Dwarf_Signed i = 0; i < atcnt; ++i)
										{
											if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
											{
												if (dwarf_attr(return_die, return_attr, &return_attr1, &error) == DW_DLV_OK)
												{
													switch (return_attr)
													{
														// start address
													case DW_AT_low_pc:
														if (dwarf_lowpc(return_die, &return_lowpc, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].StartPC = return_lowpc;
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].LowPC = return_lowpc;
														}
														break;

														// end address
													case DW_AT_high_pc:
														if (dwarf_highpc(return_die, &return_highpc, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].HighPC = return_highpc;
														}
														break;

														// Line number
													case DW_AT_decl_line:
														if (dwarf_formudata(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NumLineSrc = return_uvalue;
														}
														break;

														// Frame
													case DW_AT_frame_base:
														if (dwarf_formudata(return_attr1, &return_uvalue, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].FrameBase = return_uvalue;
															PtrCU[NbCU].NbFrames++;
														}
														break;

														// function name
													case DW_AT_name:
														if (dwarf_formstring(return_attr1, &return_string, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrSubprogramName = (char *)calloc(strlen(return_string) + 1, 1);
															strcpy(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrSubprogramName, return_string);
															dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
														}
														break;

													case DW_AT_sibling:
														break;

													case DW_AT_GNU_all_tail_call_sites:
														break;

													case DW_AT_type:
														break;

													case DW_AT_prototyped:
														break;

														// File number
													case DW_AT_decl_file:
														break;

													case DW_AT_external:
														break;

													default:
														break;
													}
												}
											}
											dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
										}
										dwarf_dealloc(dbg, atlist, DW_DLA_LIST);

										// Get source line number and associated block of address
										for (Dwarf_Signed i = 0; i < cnt; ++i)
										{
											// Check the presence of the line in the memory frame
											if (PtrCU[NbCU].PtrUsedLinesSrc && (PtrCU[NbCU].PtrUsedLinesSrc[i].StartPC >= return_lowpc) && (PtrCU[NbCU].PtrUsedLinesSrc[i].StartPC <= return_highpc))
											{
												PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc = (DMIStruct_LineSrc *)realloc(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc, (PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbLinesSrc + 1) * sizeof(DMIStruct_LineSrc));
												memset((void *)(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc + PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbLinesSrc), 0, sizeof(DMIStruct_LineSrc));
												PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbLinesSrc].StartPC = PtrCU[NbCU].PtrUsedLinesSrc[i].StartPC;
												PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrLinesSrc[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbLinesSrc].NumLineSrc = PtrCU[NbCU].PtrUsedLinesSrc[i].NumLineSrc;
												PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbLinesSrc++;
											}
										}

										if (dwarf_child(return_die, &return_subdie, &error) == DW_DLV_OK)
										{
											do
											{
												return_sub = return_subdie;
												if ((dwarf_tag(return_subdie, &return_tagval, &error) == DW_DLV_OK))
												{
													switch (return_tagval)
													{
													case DW_TAG_formal_parameter:
													case DW_TAG_variable:
														if (dwarf_attrlist(return_subdie, &atlist, &atcnt, &error) == DW_DLV_OK)
														{
															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables = (VariablesStruct *)realloc(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables, ((PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables + 1) * sizeof(VariablesStruct)));
															memset(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables + PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables, 0, sizeof(VariablesStruct));

															for (Dwarf_Signed i = 0; i < atcnt; ++i)
															{
																if (dwarf_whatattr(atlist[i], &return_attr, &error) == DW_DLV_OK)
																{
																	if (dwarf_attr(return_subdie, return_attr, &return_attr1, &error) == DW_DLV_OK)
																	{
																		switch (return_attr)
																		{
																		case DW_AT_location:
																			if (dwarf_formblock(return_attr1, &return_block, &error) == DW_DLV_OK)
																			{
																				PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].Op = *((unsigned char *)(return_block->bl_data));

																				switch (return_block->bl_len)
																				{
																				case 1:
																					break;

																				case 2:
																				case 3:
																				case 4:
																				case 5:
																					switch (return_tagval)
																					{
																					case DW_TAG_variable:
																						PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].Offset = ReadLEB128((char *)return_block->bl_data + 1);
																						break;

																					case DW_TAG_formal_parameter:
																						PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].Offset = ReadULEB128((char *)return_block->bl_data + 1);
																						break;

																					default:
																						break;
																					}
																					break;

																				default:
																					break;
																				}
																				dwarf_dealloc(dbg, return_block, DW_DLA_BLOCK);
																			}
																			break;

																		case DW_AT_type:
																			if (dwarf_global_formref(return_attr1, &return_offset, &error) == DW_DLV_OK)
																			{
																				PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].TypeOffset = return_offset;
																			}
																			break;

																		case DW_AT_name:
																			if (dwarf_formstring(return_attr1, &return_string, &error) == DW_DLV_OK)
																			{
#ifdef DEBUG_VariableName
																				if (!strcmp(return_string, DEBUG_VariableName))
#endif
																				{
																					PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].PtrName = (char *)calloc(strlen(return_string) + 1, 1);
																					strcpy(PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].PtrVariables[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables].PtrName, return_string);
																				}
																				dwarf_dealloc(dbg, return_string, DW_DLA_STRING);
																			}
																			break;

																		case DW_AT_decl_file:
																			break;

																		case DW_AT_decl_line:
																			break;

																		default:
																			break;
																		}
																	}
																}

																dwarf_dealloc(dbg, atlist[i], DW_DLA_ATTR);
															}

															PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs].NbVariables++;

															dwarf_dealloc(dbg, atlist, DW_DLA_LIST);
														}
														break;

													case DW_TAG_label:
														break;

													default:
														break;
													}
												}
											}
											while (dwarf_siblingof(dbg, return_sub, &return_subdie, &error) == DW_DLV_OK);
										}

										PtrCU[NbCU].NbSubProgs++;
									}
									break;

								default:
									break;
								}
							}
						}
						while (dwarf_siblingof(dbg, return_sib, &return_die, &error) == DW_DLV_OK);
					}
				}

				// Set the source code lines
				if (PtrCU[NbCU].NbLinesLoadSrc)
				{
					if (PtrCU[NbCU].PtrLinesLoadSrc = (char **)calloc(PtrCU[NbCU].NbLinesLoadSrc, sizeof(char *)))
					{
						for (size_t j = 0; j < PtrCU[NbCU].NbLinesLoadSrc; j++)
						{
							if (PtrCU[NbCU].PtrLinesLoadSrc[j] = (char *)calloc(10000, sizeof(char)))
							{
								if (Ptr = DWARFManager_GetLineSrcFromNumLine(PtrCU[NbCU].PtrLoadSrc, (j + 1)))
								{
#ifndef CONVERT_QT_HML
									strcpy(PtrCU[NbCU].PtrLinesLoadSrc[j], Ptr);
#else
									size_t i = 0;

									while (*Ptr)
									{
										switch (*Ptr)
										{
										case 9:
											strcat(PtrCU[NbCU].PtrLinesLoadSrc[j], "&nbsp;");
											i += 6;
											break;

										case '<':
											strcat(PtrCU[NbCU].PtrLinesLoadSrc[j], "&lt;");
											i += 4;
											break;

										case '>':
											strcat(PtrCU[NbCU].PtrLinesLoadSrc[j], "&gt;");
											i += 4;
											break;
#if 0
										case '&':
											strcpy(PtrCU[NbCU].PtrLinesLoadSrc[j], "&amp;");
											i += strlen("&amp;");
											break;
#endif
#if 0
										case '"':
											strcpy(PtrCU[NbCU].PtrLinesLoadSrc[j], "&quot;");
											i += strlen("&quot;");
											break;
#endif
										default:
											PtrCU[NbCU].PtrLinesLoadSrc[j][i++] = *Ptr;
											break;
										}
										Ptr++;
									}
#endif
								}
								PtrCU[NbCU].PtrLinesLoadSrc[j] = (char *)realloc(PtrCU[NbCU].PtrLinesLoadSrc[j], strlen(PtrCU[NbCU].PtrLinesLoadSrc[j]) + 1);
							}
						}

						// Init lines source information for each source code line numbers and for each subprogs
						for (size_t j = 0; j < PtrCU[NbCU].NbSubProgs; j++)
						{
							// Check if the subprog / function's line exists in the source code
							if (PtrCU[NbCU].PtrSubProgs[j].NumLineSrc <= PtrCU[NbCU].NbLinesLoadSrc)
							{
								PtrCU[NbCU].PtrSubProgs[j].PtrLineSrc = PtrCU[NbCU].PtrLinesLoadSrc[PtrCU[NbCU].PtrSubProgs[j].NumLineSrc - 1];
							}

							for (size_t k = 0; k < PtrCU[NbCU].PtrSubProgs[j].NbLinesSrc; k++)
							{
								if (PtrCU[NbCU].PtrSubProgs[j].PtrLinesSrc[k].NumLineSrc <= PtrCU[NbCU].NbLinesLoadSrc)
								{
									PtrCU[NbCU].PtrSubProgs[j].PtrLinesSrc[k].PtrLineSrc = PtrCU[NbCU].PtrLinesLoadSrc[PtrCU[NbCU].PtrSubProgs[j].PtrLinesSrc[k].NumLineSrc - 1];
								}
							}
						}
					}
				}
				else
				{
					// Set each source lines pointer to NULL
					if (PtrCU[NbCU].NbSubProgs)
					{
						// Check the presence of source lines dedicated to the sub progs
						if (PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs - 1].NbLinesSrc)
						{
							size_t i = PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs - 1].PtrLinesSrc[PtrCU[NbCU].PtrSubProgs[PtrCU[NbCU].NbSubProgs - 1].NbLinesSrc - 1].NumLineSrc;
							if (PtrCU[NbCU].PtrLinesLoadSrc = (char **)calloc(i, sizeof(char *)))
							{
								for (size_t j = 0; j < i; j++)
								{
									PtrCU[NbCU].PtrLinesLoadSrc[j] = NULL;
								}
							}
						}
					}
				}

				// Check validity between used number lines and number lines in the source file
				if (PtrCU[NbCU].LastNumUsedLinesSrc <= PtrCU[NbCU].NbLinesLoadSrc)
				{
					// Set information based on used line numbers
					if (PtrCU[NbCU].PtrUsedLinesSrc)
					{
						// Set the line source pointers for each used line numbers
						if (PtrCU[NbCU].PtrLinesLoadSrc)
						{
							for (size_t i = 0; i < PtrCU[NbCU].NbUsedLinesSrc; i++)
							{
								PtrCU[NbCU].PtrUsedNumLines[i] = PtrCU[NbCU].PtrUsedLinesSrc[i].NumLineSrc - 1;
								PtrCU[NbCU].PtrUsedLinesLoadSrc[i] = PtrCU[NbCU].PtrUsedLinesSrc[i].PtrLineSrc = PtrCU[NbCU].PtrLinesLoadSrc[PtrCU[NbCU].PtrUsedLinesSrc[i].NumLineSrc - 1];
							}

							// Setup memory range for the code if CU doesn't have already this information
							// It is taken from the used lines structure
							if (!PtrCU[NbCU].LowPC && (!PtrCU[NbCU].HighPC || (PtrCU[NbCU].HighPC == ~0)))
							{
								PtrCU[NbCU].LowPC = PtrCU[NbCU].PtrUsedLinesSrc[0].StartPC;
								PtrCU[NbCU].HighPC = PtrCU[NbCU].PtrUsedLinesSrc[PtrCU[NbCU].NbUsedLinesSrc - 1].StartPC;
							}
						}
					}
				}

				// Init global variables information based on types information
				for (size_t i = 0; i < PtrCU[NbCU].NbVariables; i++)
				{
					DWARFManager_InitInfosVariable(PtrCU[NbCU].PtrVariables + i);
				}

				// Init local variables information based on types information
				for (size_t i = 0; i < PtrCU[NbCU].NbSubProgs; i++)
				{
					for (size_t j = 0; j < PtrCU[NbCU].PtrSubProgs[i].NbVariables; j++)
					{
						DWARFManager_InitInfosVariable(PtrCU[NbCU].PtrSubProgs[i].PtrVariables + j);
					}
				}
			}

			++NbCU;
		}
	} 
}


// Conform slashes and backslashes
void DWARFManager_ConformSlachesBackslashes(char *Ptr)
{
	while (*Ptr)
	{
#if defined(_WIN32)
		if (*Ptr == '/')
		{
			*Ptr = '\\';
		}
#else
		if (*Ptr == '\\')
		{
			*Ptr = '/';
		}
#endif
		Ptr++;
	}
}


// Variables information initialisation
void DWARFManager_InitInfosVariable(VariablesStruct *PtrVariables)
{
#ifdef DEBUG_VariableName
	if (PtrVariables->PtrName && !strcmp(PtrVariables->PtrName, DEBUG_VariableName))
#endif
	{
		PtrVariables->PtrTypeName = (char *)calloc(1000, 1);
		size_t TypeOffset = PtrVariables->TypeOffset;

		for (size_t j = 0; j < PtrCU[NbCU].NbTypes; j++)
		{
			if (TypeOffset == PtrCU[NbCU].PtrTypes[j].Offset)
			{
				switch (PtrCU[NbCU].PtrTypes[j].Tag)
				{
					// subroutine / function pointer
				case DW_TAG_subroutine_type:
					PtrVariables->TypeTag |= TypeTag_subroutine_type;
					strcat(PtrVariables->PtrTypeName, " (* ) ()");
					break;

					// structure & union type tag
				case DW_TAG_structure_type:
				case DW_TAG_union_type:
					PtrVariables->TypeTag |= (PtrCU[NbCU].PtrTypes[j].Tag == DW_TAG_structure_type) ? TypeTag_structure : TypeTag_union;
					if (!(PtrVariables->TypeTag & TypeTag_typedef))
					{
						if (PtrCU[NbCU].PtrTypes[j].PtrName)
						{
							strcat(PtrVariables->PtrTypeName, PtrCU[NbCU].PtrTypes[j].PtrName);
						}
					}
					if ((TypeOffset = PtrCU[NbCU].PtrTypes[j].TypeOffset))
					{
						j = -1;
					}
					else
					{
						if ((PtrVariables->TypeTag & TypeTag_pointer))
						{
							strcat(PtrVariables->PtrTypeName, "* ");
						}

						if (PtrVariables->Op)
						{
							// fill the structure members
							PtrVariables->TabVariables = (VariablesStruct**)calloc(PtrCU[NbCU].PtrTypes[j].NbStructureMembers, sizeof(VariablesStruct*));
							for (size_t i = 0; i < PtrCU[NbCU].PtrTypes[j].NbStructureMembers; i++)
							{
								//if (PtrVariables->PtrName != PtrCU[NbCU].PtrTypes[j].PtrStructureMembers[i].PtrName)
								{
									PtrVariables->TabVariables[PtrVariables->NbTabVariables] = (VariablesStruct*)calloc(1, sizeof(VariablesStruct));
									PtrVariables->TabVariables[PtrVariables->NbTabVariables]->PtrName = PtrCU[NbCU].PtrTypes[j].PtrStructureMembers[i].PtrName;
									PtrVariables->TabVariables[PtrVariables->NbTabVariables]->TypeOffset = PtrCU[NbCU].PtrTypes[j].PtrStructureMembers[i].TypeOffset;
									PtrVariables->TabVariables[PtrVariables->NbTabVariables]->Offset = (int)PtrCU[NbCU].PtrTypes[j].PtrStructureMembers[i].DataMemberLocation;
									DWARFManager_InitInfosVariable(PtrVariables->TabVariables[PtrVariables->NbTabVariables++]);
								}
							}
						}
					}
					break;

					// pointer type tag
				case DW_TAG_pointer_type:
					PtrVariables->TypeTag |= TypeTag_pointer;
					PtrVariables->TypeByteSize = PtrCU[NbCU].PtrTypes[j].ByteSize;
					PtrVariables->TypeEncoding = 0x10;
					if (!(TypeOffset = PtrCU[NbCU].PtrTypes[j].TypeOffset))
					{
						strcat(PtrVariables->PtrTypeName, "void* ");
					}
					else
					{
						j = -1;
					}
					break;

				case DW_TAG_enumeration_type:
					PtrVariables->TypeTag |= TypeTag_enumeration_type;
					PtrVariables->TypeByteSize = PtrCU[NbCU].PtrTypes[j].ByteSize;
					if (!(PtrVariables->TypeEncoding = PtrCU[NbCU].PtrTypes[j].Encoding))
					{
						// Try to determine the possible size
						switch (PtrVariables->TypeByteSize)
						{
						case 4:
							PtrVariables->TypeEncoding = 0x7;
							break;

						default:
							break;
						}
					}
					break;

					// Typedef type tag
				case DW_TAG_typedef:
					if (!(PtrVariables->TypeTag & TypeTag_typedef))
					{
						PtrVariables->TypeTag |= TypeTag_typedef;
						strcat(PtrVariables->PtrTypeName, PtrCU[NbCU].PtrTypes[j].PtrName);
					}
					if ((TypeOffset = PtrCU[NbCU].PtrTypes[j].TypeOffset))
					{
						j = -1;
					}
					break;

					// ? type tag
				case DW_TAG_subrange_type:
					PtrVariables->TypeTag |= TypeTag_subrange;
					break;

					// Array type tag
				case DW_TAG_array_type:
					PtrVariables->TypeTag |= TypeTag_arraytype;
					if ((TypeOffset = PtrCU[NbCU].PtrTypes[j].TypeOffset))
					{
						j = -1;
					}
					break;

					// Const type tag
				case DW_TAG_const_type:
					PtrVariables->TypeTag |= TypeTag_consttype;
					strcat(PtrVariables->PtrTypeName, "const ");
					if ((TypeOffset = PtrCU[NbCU].PtrTypes[j].TypeOffset))
					{
						j = -1;
					}
					break;

					// Base type tag
				case DW_TAG_base_type:
					if (!(PtrVariables->TypeTag & TypeTag_typedef))
					{
						strcat(PtrVariables->PtrTypeName, PtrCU[NbCU].PtrTypes[j].PtrName);
					}
					if ((PtrVariables->TypeTag & TypeTag_pointer))
					{
						strcat(PtrVariables->PtrTypeName, "* ");
					}
					else
					{
						PtrVariables->TypeByteSize = PtrCU[NbCU].PtrTypes[j].ByteSize;
						PtrVariables->TypeEncoding = PtrCU[NbCU].PtrTypes[j].Encoding;
					}
					if ((PtrVariables->TypeTag & TypeTag_arraytype))
					{
						strcat(PtrVariables->PtrTypeName, "[]");
					}
					break;

				default:
					break;
				}
			}
		}
	}
}


// Get symbol name based from address
// Return NULL if no symbol name exists
char *DWARFManager_GetSymbolnameFromAdr(size_t Adr)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((PtrCU[i].PtrSubProgs[j].StartPC == Adr))
				{
					return PtrCU[i].PtrSubProgs[j].PtrSubprogramName;
				}
			}
		}
	}

	return NULL;
}


// Get complete source filename based from address
// Return NULL if no source filename exists
// Return the existence status in Status if pointer not NULL
char *DWARFManager_GetFullSourceFilenameFromAdr(size_t Adr, DWARFstatus *Status)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			if (Status)
			{
				*Status = PtrCU[i].Status;
			}

			return PtrCU[i].PtrFullFilename;
		}
	}

	return	NULL;
}


// Get text line source based on line number (starting from 1)
// Return NULL if no text line exists or if line number is 0
char *DWARFManager_GetLineSrcFromNumLine(char *PtrSrcFile, size_t NumLine)
{
	size_t i = 0;
	char *PtrLineSrc = NULL;

	if (PtrSrcFile)
	{
		while (i != NumLine)
		{
			PtrLineSrc = PtrSrcFile;
			while (*PtrSrcFile++);
			i++;
		}
	}

	return PtrLineSrc;
}


// Get number of variables
// A NULL address will return the numbre of global variables, otherwise it will return the number of local variables
size_t DWARFManager_GetNbVariables(size_t Adr)
{
	// check the address
	if (Adr)
	{
		for (size_t i = 0; i < NbCU; i++)
		{
			if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
			{
				for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
				{
					if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
					{
						return PtrCU[i].PtrSubProgs[j].NbVariables;
					}
				}
			}
		}
	}
	else
	{
		size_t NbVariables = 0;

		for (size_t i = 0; i < NbCU; i++)
		{
			NbVariables += PtrCU[i].NbVariables;
		}

		return NbVariables;
	}

	return 0;
#if 0
	return Adr ? DWARFManager_GetNbLocalVariables(Adr) : DWARFManager_GetNbGlobalVariables();
#endif
}


// Get variable's information
// A NULL address will return the pointer to the global variable structure, otherwise it will return the local's one
void* DWARFManager_GetInfosVariable(size_t Adr, size_t Index)
{
	// check the address
	if (Adr)
	{
		// get the pointer's information from a local variable
		for (size_t i = 0; i < NbCU; i++)
		{
			if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
			{
				for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
				{
					if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
					{
						return &PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1];
					}
				}
			}
		}
	}
	else
	{
		// get the pointer's information from a global variable
		for (size_t i = 0; i < NbCU; i++)
		{
			if (PtrCU[i].NbVariables)
			{
				if (Index <= PtrCU[i].NbVariables)
				{
					return &PtrCU[i].PtrVariables[Index - 1];
				}
				else
				{
					Index -= PtrCU[i].NbVariables;
				}
			}
		}
	}

	return NULL;
}


// Get global variable memory address based on his name
// Return 0 if not found, or will return the first occurence found
size_t DWARFManager_GetGlobalVariableAdrFromName(char *VariableName)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			for (size_t j = 0; j < PtrCU[i].NbVariables; j++)
			{
				if (!strcmp(PtrCU[i].PtrVariables[j].PtrName, VariableName))
				{
					return PtrCU[i].PtrVariables[j].Addr;
				}
			}
		}
	}

	return 0;
}


#if 0
// Get number of variables referenced by the function range address
size_t DWARFManager_GetNbLocalVariables(size_t Adr)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].NbVariables;
				}
			}
		}
	}

	return 0;
}


// Get Compilation Unit / global variables numbers
// Return number of variables
size_t DWARFManager_GetNbGlobalVariables(void)
{
	size_t NbVariables = 0;

	for (size_t i = 0; i < NbCU; i++)
	{
		NbVariables += PtrCU[i].NbVariables;
	}

	return NbVariables;
}


// Get local variable name based on his index (starting from 1)
// Return name's pointer text found
// Return NULL if not found
char *DWARFManager_GetLocalVariableName(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].PtrName;
				}
			}
		}
	}

	return NULL;
}


// Get local variable's type tag based on his index (starting from 1)
// Return 0 if not found
size_t DWARFManager_GetLocalVariableTypeTag(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].TypeTag;
				}
			}
		}
	}

	return 0;
}


// Get the local variable's offset based on a index (starting from 1)
// Return 0 if no offset has been found
int DWARFManager_GetLocalVariableOffset(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].Offset;
				}
			}
		}
	}

	return 0;
}


// Get local variable Type Byte Size based on his address and index (starting from 1)
// Return 0 if not found
// May return 0 if there is no Type Byte Size linked to the variable's address and index
size_t DWARFManager_GetLocalVariableTypeByteSize(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].TypeByteSize;
				}
			}
		}
	}

	return 0;
}


// Get local variable Type Encoding based on his address and index (starting from 1)
// Return 0 if not found
// May return 0 if there is no Type Encoding linked to the variable's address and index
size_t DWARFManager_GetLocalVariableTypeEncoding(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].TypeEncoding;
				}
			}
		}
	}

	return 0;
}


// Get local variable Op based on his address and index (starting from 1)
// Return 0 if not found, may return 0 if there isn't Op linked to the variable's index
size_t DWARFManager_GetLocalVariableOp(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].Op;
				}
			}
		}
	}

	return 0;
}


// Get local variable type name based on his index (starting from 1) and an address
// Return NULL if not found, may also return NULL if there is no type linked to the variable's index
char *DWARFManager_GetLocalVariableTypeName(size_t Adr, size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrVariables[Index - 1].PtrTypeName;
				}
			}
		}
	}

	return NULL;
}


// Get global variable type name based on his index (starting from 1)
// Return NULL if not found
// May return NULL if there is not type linked to the variable's index
char *DWARFManager_GetGlobalVariableTypeName(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].PtrTypeName;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return NULL;
}


// Get global variable's type tag based on his index (starting from 1)
// Return 0 if not found
size_t DWARFManager_GetGlobalVariableTypeTag(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].TypeTag;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return 0;
}


// Get global variable byte size based on his index (starting from 1)
// Return 0 if not found
size_t DWARFManager_GetGlobalVariableTypeByteSize(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].TypeByteSize;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return 0;
}


// Get global variable encoding based on his index (starting from 1)
// Return 0 if not found
size_t DWARFManager_GetGlobalVariableTypeEncoding(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].TypeEncoding;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return 0;
}


// Get global variable memory address based on his index (starting from 1)
// Return 0 if not found
size_t DWARFManager_GetGlobalVariableAdr(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].Addr;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return 0;
}


// Get global variable name based on his index (starting from 1)
// Return name's pointer text found, or will return NULL if no variable can be found
char *DWARFManager_GetGlobalVariableName(size_t Index)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if (PtrCU[i].NbVariables)
		{
			if (Index <= PtrCU[i].NbVariables)
			{
				return PtrCU[i].PtrVariables[Index - 1].PtrName;
			}
			else
			{
				Index -= PtrCU[i].NbVariables;
			}
		}
	}

	return NULL;
}
#endif


// Get text line from source based on address and his tag
// A tag can be either 0 or a DW_TAG_subprogram
// DW_TAG_subprogram will look for the line pointing to the function
// Return NULL if no text line has been found
char *DWARFManager_GetLineSrcFromAdr(size_t Adr, size_t Tag)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					if ((PtrCU[i].PtrSubProgs[j].StartPC == Adr) && (!Tag || (Tag == DW_TAG_subprogram)))
					{
						return PtrCU[i].PtrSubProgs[j].PtrLineSrc;
					}
					else
					{
						for (size_t k = 0; k < PtrCU[i].PtrSubProgs[j].NbLinesSrc; k++)
						{
							if (PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].StartPC <= Adr)
							{
								if ((PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].StartPC == Adr) && (!Tag || (PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].Tag == Tag)))
								{
									return PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].PtrLineSrc;
								}
							}
							else
							{
								return PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k - 1].PtrLineSrc;
							}
						}
					}
				}
			}
		}
	}

	return	NULL;
}


// Get line number based on the address and a tag
// A tag can be either 0 or a DW_TAG_subprogram
// DW_TAG_subprogram will look for the line pointing to the function name as described in the source code
// Return 0 if no line number has been found
size_t DWARFManager_GetNumLineFromAdr(size_t Adr, size_t Tag)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					if ((PtrCU[i].PtrSubProgs[j].StartPC == Adr) && (!Tag || (Tag == DW_TAG_subprogram)))
					{
						return PtrCU[i].PtrSubProgs[j].NumLineSrc;
					}
					else
					{
						for (size_t k = 0; k < PtrCU[i].PtrSubProgs[j].NbLinesSrc; k++)
						{
							if (PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].StartPC <= Adr)
							{
								if ((PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].StartPC == Adr) && (!Tag || (PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].Tag == Tag)))
								{
									return PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].NumLineSrc;
								}
							}
							else
							{
								return PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k - 1].NumLineSrc;
							}
						}
					}
#if 0
					if (!Tag || (Tag == DW_TAG_subprogram))
					{
						return PtrCU[i].PtrSubProgs[j].NumLineSrc;
					}
#endif
				}
			}

			// Check if a used line is found with the address
			for (size_t j = 0; j < PtrCU[i].NbUsedLinesSrc; j++)
			{
				if (PtrCU[i].PtrUsedLinesSrc[j].StartPC == Adr)
				{
					return PtrCU[i].PtrUsedLinesSrc[j].NumLineSrc;
				}
			}
		}
	}

	return	0;
}


// Get function name based on an address
// Return NULL if no function name has been found, otherwise will return the function name in the range of the provided address
char *DWARFManager_GetFunctionName(size_t Adr)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					return PtrCU[i].PtrSubProgs[j].PtrSubprogramName;
				}
			}
		}
	}

	return NULL;
}


// Get number of lines of texts source list from source index
size_t DWARFManager_GetSrcNbListPtrFromIndex(size_t Index, bool Used)
{
	if (!Used)
	{
		return PtrCU[Index].NbLinesLoadSrc;
	}
	else
	{
		return PtrCU[Index].NbUsedLinesSrc;
	}
}


// Get text source line number list pointer from source index
// Return NULL for the text source used list 
size_t *DWARFManager_GetSrcNumLinesPtrFromIndex(size_t Index, bool Used)
{
	if (Used)
	{
		return	PtrCU[Index].PtrUsedNumLines;
	}
	else
	{
		return NULL;
	}
}


// Get text source list pointers from source index
// Return NULL for the text source used list 
char **DWARFManager_GetSrcListPtrFromIndex(size_t Index, bool Used)
{
	if (!Used)
	{
		return PtrCU[Index].PtrLinesLoadSrc;
	}
	else
	{
		return PtrCU[Index].PtrUsedLinesLoadSrc;
	}
}


// Get source language
size_t DWARFManager_GetSrcLanguageFromIndex(size_t Index)
{
	return PtrCU[Index].Language;
}


// Get text line from source based on address and num line (starting from 1)
// Return NULL if no text line has been found
char *DWARFManager_GetLineSrcFromAdrNumLine(size_t Adr, size_t NumLine)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			for (size_t j = 0; j < PtrCU[i].NbSubProgs; j++)
			{
				if ((Adr >= PtrCU[i].PtrSubProgs[j].LowPC) && (Adr < PtrCU[i].PtrSubProgs[j].HighPC))
				{
					if (PtrCU[i].PtrSubProgs[j].NumLineSrc == NumLine)
					{
						return PtrCU[i].PtrSubProgs[j].PtrLineSrc;
					}
					else
					{
						for (size_t k = 0; k < PtrCU[i].PtrSubProgs[j].NbLinesSrc; k++)
						{
							if (PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].NumLineSrc == NumLine)
							{
								return PtrCU[i].PtrSubProgs[j].PtrLinesSrc[k].PtrLineSrc;
							}
						}
					}
				}
			}
		}
	}

	return NULL;
}


// Get text line pointer from source, based on address and line number (starting from 1)
// Return NULL if no text line has been found, or if requested number line is above the source total number of lines
char *DWARFManager_GetLineSrcFromNumLineBaseAdr(size_t Adr, size_t NumLine)
{
	for (size_t i = 0; i < NbCU; i++)
	{
		if ((Adr >= PtrCU[i].LowPC) && (Adr < PtrCU[i].HighPC))
		{
			if (NumLine <= PtrCU[i].NbLinesLoadSrc)
			{
				return PtrCU[i].PtrLinesLoadSrc[NumLine - 1];
			}
			else
			{
				return NULL;
			}
		}
	}

	return NULL;
}


// Get number of source code filenames
size_t DWARFManager_GetNbSources(void)
{
	return NbCU;
}


// Get source code filename, including his directory, based on index (starting from 0)
char *DWARFManager_GetNumFullSourceFilename(size_t Index)
{
	return (PtrCU[Index].PtrFullFilename);
}


// Get source code filename based on index (starting from 0)
char *DWARFManager_GetNumSourceFilename(size_t Index)
{
	return (PtrCU[Index].PtrSourceFilename);
}

