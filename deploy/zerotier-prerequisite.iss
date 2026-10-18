[Code]
var
  ZeroTierRestartRequired: Boolean;

function CompareVersion(A, B: String; var Valid: Boolean): Integer;
var
  I, P, VA, VB: Integer;
  PartA, PartB: String;
begin
  Result := 0;
  Valid := (A <> '') and (B <> '');
  for I := 0 to 3 do begin
    P := Pos('.', A);
    if P > 0 then begin PartA := Copy(A, 1, P - 1); Delete(A, 1, P); end
    else begin PartA := A; A := ''; end;
    P := Pos('.', B);
    if P > 0 then begin PartB := Copy(B, 1, P - 1); Delete(B, 1, P); end
    else begin PartB := B; B := ''; end;
    if PartA = '' then VA := 0 else VA := StrToIntDef(PartA, -1);
    if PartB = '' then VB := 0 else VB := StrToIntDef(PartB, -1);
    if (VA < 0) or (VB < 0) then Valid := False;
    if Result = 0 then begin
      if VA > VB then Result := 1;
      if VA < VB then Result := -1;
    end;
  end;
  if (A <> '') or (B <> '') then Valid := False;
end;

function ReadZeroTierView(Root: Integer; var Found: Boolean; var BestVersion: String): Boolean;
var
  Keys: TArrayOfString;
  I, Comparison: Integer;
  Key, Name, Publisher, Version: String;
  Valid: Boolean;
  WindowsInstaller: Cardinal;
begin
  Result := True;
  Key := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall';
  if not RegGetSubkeyNames(Root, Key, Keys) then begin
    Result := not RegKeyExists(Root, Key);
    Exit;
  end;
  for I := 0 to GetArrayLength(Keys) - 1 do begin
    Key := 'SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\' + Keys[I];
    if RegQueryStringValue(Root, Key, 'DisplayName', Name) and
       ((CompareText(Name, 'ZeroTier One') = 0) or (CompareText(Name, 'ZeroTier') = 0)) then begin
      if not RegQueryStringValue(Root, Key, 'Publisher', Publisher) then begin Result := False; Exit; end;
      Publisher := Lowercase(Trim(Publisher));
      StringChangeEx(Publisher, ' ', '', True);
      StringChangeEx(Publisher, ',', '', True);
      StringChangeEx(Publisher, '.', '', True);
      if Publisher = 'zerotierinc' then begin
        if not RegQueryDWordValue(Root, Key, 'WindowsInstaller', WindowsInstaller) or (WindowsInstaller <> 1) then begin Result := False; Exit; end;
        if not RegQueryStringValue(Root, Key, 'DisplayVersion', Version) then begin Result := False; Exit; end;
        Comparison := CompareVersion(Version, '{#ZeroTierVersion}', Valid);
        if not Valid then begin Result := False; Exit; end;
        if Found then begin
          Comparison := CompareVersion(Version, BestVersion, Valid);
          if not Valid then begin Result := False; Exit; end;
          if Comparison > 0 then BestVersion := Version;
        end else BestVersion := Version;
        Found := True;
      end;
    end;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var
  Found, Registered, Valid: Boolean;
  InstalledVersion, MsiPath, Parameters, UiFlags: String;
  Comparison, ExitCode: Integer;
  PackageLock: TFileStream;
begin
  Result := '';
  if not WizardIsTaskSelected('zerotier') then begin Log('ZT_PREREQ_OPT_OUT'); Exit; end;
  Found := False;
  InstalledVersion := '';
  if not ReadZeroTierView(HKLM32, Found, InstalledVersion) then begin
    Result := '无法可靠读取已有 ZeroTier 版本。请检查已安装的应用，或返回并取消勾选 ZeroTier 后仅安装 GameLink。'; Exit;
  end;
  if IsWin64 then
    if not ReadZeroTierView(HKLM64, Found, InstalledVersion) then begin
      Result := '无法可靠读取已有 ZeroTier 版本。请检查已安装的应用，或返回并取消勾选 ZeroTier 后仅安装 GameLink。'; Exit;
    end;
  Registered := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\ZeroTierOneService');
  Comparison := -1;
  if Found then begin
    Comparison := CompareVersion(InstalledVersion, '{#ZeroTierVersion}', Valid);
    if not Valid then begin Result := '已有 ZeroTier 版本无法识别，已停止自动安装。'; Exit; end;
    if (Comparison >= 0) and Registered then begin
      Log('ZT_PREREQ_SKIP: keeping ZeroTier ' + InstalledVersion); Exit;
    end;
    if Comparison > 0 then begin
      Result := '已有更新的 ZeroTier，但服务注册异常。请使用该版本的官方安装器修复；GameLink 不会降级安装。'; Exit;
    end;
  end else if Registered then begin
    Result := '检测到不属于标准 MSI 安装的 ZeroTier 服务。请返回取消勾选 ZeroTier 以保留已有服务，或先使用原安装方式管理它。'; Exit;
  end;
  try
    ExtractTemporaryFile('ZeroTier-One.msi');
    MsiPath := ExpandConstant('{tmp}\ZeroTier-One.msi');
    { $20 = fmOpenRead + fmShareDenyWrite; keep the verified file locked during UAC/MSI. }
    PackageLock := TFileStream.Create(MsiPath, $20);
    try
      if Lowercase(GetSHA256OfFile(MsiPath)) <> '{#ZeroTierSHA256}' then begin
        Result := 'ZeroTier 安装包校验失败，已停止安装。请重新获取完整 GameLink 安装包。'; Exit;
      end;
      if WizardSilent then UiFlags := '/qn' else UiFlags := '/qb';
      Parameters := '/i "' + MsiPath + '" ' + UiFlags + ' /norestart REBOOT=ReallySuppress';
      if Found and (Comparison = 0) then Parameters := Parameters + ' REINSTALL=ALL REINSTALLMODE=vomus';
      Log('ZT_PREREQ_INSTALL: official ZeroTier {#ZeroTierVersion}');
      if not ShellExec('runas', ExpandConstant('{sys}\msiexec.exe'), Parameters, '', SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode) then begin
        if ExitCode = 1223 then Result := '已取消管理员授权，ZeroTier 未安装。请重试，或返回取消勾选 ZeroTier。'
        else Result := '无法启动 Windows Installer：' + SysErrorMessage(ExitCode);
        Exit;
      end;
      if (ExitCode = 3010) or (ExitCode = 1641) then ZeroTierRestartRequired := True
      else if ExitCode <> 0 then begin
        if ExitCode = 1602 then Result := '已取消 ZeroTier 安装。请重试，或返回取消勾选 ZeroTier。'
        else if ExitCode = 1618 then Result := '另一个 Windows 安装任务正在运行，请完成后重试。'
        else Result := 'ZeroTier 安装失败，Windows Installer 错误码 ' + IntToStr(ExitCode) + '。请检查安装窗口或系统事件日志后重试。';
        Exit;
      end;
      if not RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\ZeroTierOneService') then begin
        Result := 'Windows Installer 已结束，但未检测到 ZeroTier 服务注册。请检查系统安装日志。'; Exit;
      end;
      Log('ZT_PREREQ_SUCCESS');
    finally
      PackageLock.Free;
    end;
  except
    Result := '无法准备 ZeroTier 安装包：' + GetExceptionMessage;
  end;
end;

procedure CurPageChanged(CurPageID: Integer);
begin
  if (CurPageID = wpFinished) and ZeroTierRestartRequired then
    WizardForm.FinishedLabel.Caption := WizardForm.FinishedLabel.Caption + #13#10#13#10 +
      'ZeroTier 需要重启电脑后完全生效。请保存工作并手动重启；安装器不会主动重启。';
end;

function GetCustomSetupExitCode: Integer;
begin
  if ZeroTierRestartRequired then Result := 3010 else Result := 0;
end;
