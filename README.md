<p align="center">
  <img src="logo.png" width="128" height="128" alt="S11 Workspace logo" />
</p>

# S11 Workspace

**Galaxy Tab S11을 위한 생산성 중심 Android 작업공간입니다.** Wine과 Box64 기반 컨테이너에서 Windows 앱을 실행하고, 게임 런처가 아닌 작업 표시줄·시작 메뉴·파일 탐색기·내부 설정 앱을 제공합니다. 원본 Winlator를 그대로 포장하는 것이 아니라 Tab S11에 맞춘 별도 사용 경험을 만드는 프로젝트입니다.

> **다운로드:** [최신 공개 APK](https://github.com/kim-kyum/winlator-app/releases/latest) · [전체 릴리스](https://github.com/kim-kyum/winlator-app/releases)
>
> README를 갱신하기 전 마지막으로 확인한 공개 버전은 build 27(2026-10-12)입니다. `tab-s11` 브랜치 푸시마다 새 빌드가 생성되므로 현재 버전은 최신 릴리스 링크에서 확인하세요. 새 기능의 실제 사용 결과는 Galaxy Tab S11에서 별도로 확인해 주세요.

## 구현된 기능

- S11 작업 표시줄·시작 메뉴, Alt+Tab 창 전환, 달력·알림 영역, 통일된 내장 앱 아이콘. Windows 시작 표시는 앱 타일 배경 없이 별도 로고로 구분
- 한국어 우선 인터페이스와 S11 스타일 파일 탐색기. 외부 UI는 개선했지만 내부 파일 목록은 아직 Wine ShellView를 사용합니다.
- **내부 설정 앱:** 해상도·화면 배율, Wine 버전, 마우스 가속·포인터 속도(1–20), 테마·배경화면, 앱·글꼴 관리, `winecfg.exe` 실행
- `Win+Shift+S`를 누르고 드래그해 **선택한 영역만 캡처**. PNG는 `C:\S11\Screenshots`에 저장되어 Gallery 앱에서 확인할 수 있습니다.
- 작업 표시줄과 설정의 Wi‑Fi·Bluetooth 버튼으로 Android 시스템 설정 열기
- 명령 프롬프트는 `C:\`에서 시작
- 원본 Winlator와 별도 설치 가능; 앱과 컨테이너 데이터가 분리됩니다.

## 알려진 제한 및 진행 중인 작업

- Wi‑Fi와 Bluetooth는 Android가 관리합니다. 현재 버튼은 Android 시스템 설정으로 이동하며, S11 내부에서 무선 라디오를 직접 켜고 끄는 기능은 제공하지 않습니다.
- 설정의 마우스 포인터 속도는 **컨테이너 안 Windows 포인터 속도**입니다. 실제 마우스 센서의 하드웨어 DPI를 변경하지 않습니다. 해상도·화면 배율 등 화면 구성 변경은 컨테이너 재시작이 필요할 수 있습니다.
- 파일 탐색기 내부 목록은 Wine ShellView를 유지하고 있습니다. 전체를 자체 구현한 현대식 파일 목록으로 교체하는 작업은 아직 완료되지 않았습니다.
- Android 쪽의 게임 중심 컨트롤·프로필 기능을 정리하는 작업도 진행 대상으로 남아 있습니다.
- Direct3D 검은 화면 문제는 앱과 그래픽 API별 추가 진단이 필요합니다. Chrome·Firefox 등 x86/x64 브라우저의 성능 개선도 확정되지 않았습니다.
- Autodesk Fusion 360의 설치·실행은 호환성이 검증되지 않았으며 지원을 보장하지 않습니다.

## 기기 및 호환성

- **개발·검증 대상:** Samsung Galaxy Tab S11 계열, ARM64, MediaTek Dimensity 9400+, Mali Immortalis-G925. 다른 기기는 이 프로젝트의 지원·검증 대상이 아닙니다.
- 이 앱은 Windows 자체가 아닙니다. Windows용 x86/x64 앱의 동작은 Wine 호환성, CPU 변환, 그래픽 드라이버 및 앱별 요구사항에 따라 달라집니다.
- 그래픽 경로는 앱이 요구하는 API와 컨테이너 설정에 따라 다릅니다. WineD3D는 주로 Direct3D를 OpenGL로 변환하고, DXVK는 D3D 9/10/11을 Vulkan으로, VKD3D는 D3D 12를 Vulkan으로 변환합니다. 검은 화면이나 렌더링 오류가 발생하면 앱/API와 실제 드라이버·DX Wrapper 조합을 확인해야 합니다.
- 브라우저와 무거운 x86/x64 앱은 ARM 환경의 CPU 변환 및 호환성 계층 때문에 느릴 수 있습니다. Galaxy Tab S11은 Mali GPU를 사용하므로 Adreno 전용 Turnip 경로를 전제로 하지 않습니다.

## 설치

1. [최신 공개 릴리스](https://github.com/kim-kyum/winlator-app/releases/latest)에서 APK를 받습니다.
2. Android에 설치하고 실행합니다. 최초 실행 시 호환성 환경을 준비하는 데 시간이 걸릴 수 있습니다.
3. 컨테이너를 만든 뒤 시작 메뉴에서 앱을 실행하고, **설정**에서 작업공간을 조정합니다.

## 소스에서 빌드

Android APK 빌드에는 프로젝트에서 사용하는 JDK, Android SDK/NDK, CMake와 저장소에 포함된 앱 자산이 필요합니다.

```bash
chmod +x gradlew
./gradlew assembleDebug
```

Win32 작업 표시줄·설정·파일 탐색기·Gallery 실행 파일은 Wine의 Windows 헤더와 import libraries를 사용하는 clang/lld 빌드 스크립트로 생성합니다.

```bash
tools/s11/shell/build.sh <wine-11.x-directory> <output-directory>
```

S11 rootfs overlay를 다시 만들 때는 `zstd`가 필요합니다.

```bash
python3 tools/s11/build_overlay.py tools/s11/rootfs-overlay app/src/main/assets
```

`tab-s11` 브랜치에 푸시하면 [GitHub Actions](https://github.com/kim-kyum/winlator-app/actions/workflows/build-s11.yml)가 overlay와 APK를 빌드합니다. 저장소의 서명 설정이 가능하면 공개 릴리스도 생성합니다.

## 기반 프로젝트 및 크레딧

S11 Workspace는 [Winlator](https://github.com/brunodev85/winlator) 소스에서 출발한 별도 포크입니다. 기존 저작권·라이선스 고지와 각 구성요소의 라이선스를 유지합니다. 구성요소별 고지는 저장소의 라이선스 파일 및 해당 프로젝트 문서를 확인해 주세요.

주요 기반 프로젝트:

- [Wine](https://www.winehq.org/)
- [Box86/Box64](https://github.com/ptitSeb)
- [Mesa](https://www.mesa3d.org/) — Zink, VirGL 등
- [DXVK](https://github.com/doitsujin/dxvk)
- [VKD3D](https://gitlab.winehq.org/wine/vkd3d)
- [CNC DDraw](https://github.com/FunkyFr3sh/cnc-ddraw)
- [Selawik](https://github.com/microsoft/Selawik) 글꼴 및 기타 저장소 내 라이선스 고지 대상 자료

이 프로젝트와 기반 오픈소스 구성요소에 기여한 모든 개발자에게 감사드립니다.
