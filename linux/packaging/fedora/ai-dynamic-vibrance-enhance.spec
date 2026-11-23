Name:           ai-dynamic-vibrance-enhance
Version:        2.0.0
Release:        1%{?dist}
Summary:        Real-time visual enhancer, 16-filter shader & vkBasalt Control Hub for Linux gaming

License:        MIT
URL:            https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance
Source0:        https://github.com/arthurj119/AI-Dynamic-Vibrance-Enhance/archive/refs/tags/v%{version}.tar.gz

BuildArch:      noarch
Requires:       python3
Requires:       python3-tkinter
Recommends:     vkBasalt
Recommends:     vkBasalt(x86-32)

%description
AI Dynamic Vibrance Enhance for Linux gaming. Provides all 16 calibrated
ReShade .fx visual filters, 5 curated starter presets, Split Preview, and a
dark Control Hub GUI + CLI that configures vkBasalt globally or per game across
Fedora, Nobara, Bazzite, Wayland, X11, and Proton.

%prep
%autosetup -n AI-Dynamic-Vibrance-Enhance-%{version}

%build
# Pure Python + ReShade FX shader assets; no compilation needed.

%install
install -Dm755 linux/adv_control_hub.py %{buildroot}%{_bindir}/%{name}
install -Dm644 shaders/AIDynamicVibranceEnhance.fx %{buildroot}%{_datadir}/%{name}/shaders/AIDynamicVibranceEnhance.fx
install -Dm644 linux/shaders/ReShade.fxh %{buildroot}%{_datadir}/%{name}/shaders/ReShade.fxh
install -Dm644 linux/assets/%{name}.desktop %{buildroot}%{_datadir}/applications/%{name}.desktop
install -Dm644 linux/assets/%{name}.svg %{buildroot}%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%check
ADV_SHADER_DIR=%{buildroot}%{_datadir}/%{name}/shaders %{__python3} %{buildroot}%{_bindir}/%{name} --self-test

%files
%license LICENSE
%{_bindir}/%{name}
%{_datadir}/%{name}/
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/scalable/apps/%{name}.svg

%changelog
* Mon Nov 23 2026 arthurj119 <arthurj119@users.noreply.github.com> - 2.0.0-1
- Initial Linux vkBasalt gaming release for Fedora, Nobara, and Bazzite
