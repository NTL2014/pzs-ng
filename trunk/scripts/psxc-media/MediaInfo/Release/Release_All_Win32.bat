@rem echo off

@rem --- Clean up ---
del   MediaInfo_All_Win32.zip
rmdir MediaInfo_All_Win32 /S /Q
mkdir MediaInfo_All_Win32


@rem --- Launch all other batches ---
call Release_GUI_Win32 SkipCleanUp
call Release_CLI_Win32 SkipCleanUp
cd ..\..\MediaInfoLib\Release\
call Release_DLL_Win32 SkipCleanUp
cd ..\..\MediaInfo\Release\

@rem --- Modifying ---
move MediaInfo_GUI_Win32\MediaInfo.exe MediaInfo_GUI_Win32\MediaInfo_GUI.exe
move MediaInfo_CLI_Win32\MediaInfo.exe MediaInfo_CLI_Win32\MediaInfo_CLI.exe

@rem --- Copying ---
xcopy MediaInfo_GUI_Win32\*.* MediaInfo_All_Win32\ /S /Y
xcopy MediaInfo_CLI_Win32\*.* MediaInfo_All_Win32\ /S /Y
xcopy ..\..\MediaInfoLib\Release\MediaInfoDLL_Win32\*.* MediaInfo_All_Win32\ /S /Y

rem --- Compressing Archive ---
cd MediaInfo_All_Win32\
..\..\..\Shared\Binary\7z a -r -t7z -mx9 ..\MediaInfo_All_Win32.7z *
cd ..


@rem --- Clean up ---
rmdir MediaInfo_All_Win32\ /S /Q
rmdir MediaInfo_GUI_Win32\ /S /Q
rmdir MediaInfo_CLI_Win32\ /S /Q
rmdir ..\..\MediaInfoLib\Release\MediaInfoDLL_Win32\ /S /Q