{ Included inside [Code]. Payload and helper hashes are fixed at build time. }
#ifdef CustomZeroTierRoots
function PrepareCustomZeroTierRoots: String;
var
  ScriptPath, PlanetPath, MoonPath, Parameters: String;
  ScriptLock, PlanetLock, MoonLock: TFileStream;
  ExitCode: Integer;
begin
  Result := '';
  if not WizardIsTaskSelected('customroots') then begin Log('ZT_ROOTS_OPT_OUT'); Exit; end;
  if not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\ZeroTierOneService') then begin
    Result := '配置自建根节点需要先安装 ZeroTier。请勾选安装 ZeroTier，或取消自建根节点选项。'; Exit;
  end;
  if ZeroTierRestartRequired then begin
    Result := 'ZeroTier 安装要求重启电脑。请先手动重启，再运行安装器配置自建根节点；不要再次升级 ZeroTier。'; Exit;
  end;
  try
    ExtractTemporaryFile('apply-zerotier-roots.ps1');
    ExtractTemporaryFile('planet');
    ExtractTemporaryFile('{#CustomMoonName}');
    ScriptPath := ExpandConstant('{tmp}\apply-zerotier-roots.ps1');
    PlanetPath := ExpandConstant('{tmp}\planet');
    MoonPath := ExpandConstant('{tmp}\{#CustomMoonName}');
    ScriptLock := TFileStream.Create(ScriptPath, $20);
    try
      PlanetLock := TFileStream.Create(PlanetPath, $20);
      try
        MoonLock := TFileStream.Create(MoonPath, $20);
        try
          if (Lowercase(GetSHA256OfFile(ScriptPath)) <> '{#CustomRootsScriptSHA256}') or
             (Lowercase(GetSHA256OfFile(PlanetPath)) <> '{#CustomPlanetSHA256}') or
             (Lowercase(GetSHA256OfFile(MoonPath)) <> '{#CustomMoonSHA256}') then begin
            Result := '自建根节点文件校验失败，已停止配置。请重新下载安装包。'; Exit;
          end;
          Parameters := '-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + ScriptPath + '"';
          if not ShellExec('runas', ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'), Parameters,
                           '', SW_HIDE, ewWaitUntilTerminated, ExitCode) then begin
            if ExitCode = 1223 then Result := '已取消管理员授权，自建根节点未配置。请重试或取消自建根节点选项。'
            else Result := '无法启动根节点配置：' + SysErrorMessage(ExitCode);
            Exit;
          end;
          if ExitCode <> 0 then begin
            case ExitCode of
              20: Result := '配置根节点需要管理员授权。';
              21: Result := '内置根节点元数据无效，请重新获取安装包。';
              22: Result := 'ZeroTier 标准服务/数据目录未就绪或处于切换状态，请修复服务后重试。';
              24: Result := '根节点配置失败且自动恢复未完成。请检查服务与 ProgramData\ZeroTier\One\gamelink-root-backups 中的备份。';
            else Result := '自建根节点配置失败；如已修改文件，已尝试恢复原配置。错误码：' + IntToStr(ExitCode);
            end;
            Exit;
          end;
          Log('ZT_ROOTS_SUCCESS: verified planet and moon configured; no network joined');
        finally MoonLock.Free; end;
      finally PlanetLock.Free; end;
    finally ScriptLock.Free; end;
  except
    Result := '无法准备自建根节点配置：' + GetExceptionMessage;
  end;
end;
#endif
