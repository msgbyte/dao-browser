import SwiftUI

enum Nova {
    static let background = dynamic(0xFAFAFB, 0x17181C)
    static let surface = dynamic(0xFFFFFF, 0x1F2126)
    static let secondary = dynamic(0xF2F3F5, 0x282A30)
    static let foreground = dynamic(0x1C1E23, 0xF1F1F3)
    static let muted = dynamic(0x858993, 0x9296A0)
    static let border = dynamic(0xE9EBEF, 0x353840)
    static let animation = Animation.timingCurve(0.22, 0.61, 0.36, 1, duration: 0.33)

    private static func dynamic(_ light: UInt32, _ dark: UInt32) -> Color {
        Color(uiColor: UIColor { traits in
            let value = traits.userInterfaceStyle == .dark ? dark : light
            return UIColor(red: CGFloat((value >> 16) & 255) / 255,
                           green: CGFloat((value >> 8) & 255) / 255,
                           blue: CGFloat(value & 255) / 255, alpha: 1)
        })
    }
}

struct DaoIcon: View {
    let name: String
    var size: CGFloat = 20
    var body: some View {
        Image("lucide-" + name).renderingMode(.template).resizable().frame(width: size, height: size)
            .accessibilityHidden(true)
    }
}

struct IconButton: View {
    let icon: String
    let label: String
    let action: () -> Void
    var body: some View {
        Button(action: action) { DaoIcon(name: icon).frame(minWidth: 44, minHeight: 44).contentShape(Rectangle()) }
            .buttonStyle(.plain).accessibilityLabel(L(label))
    }
}

struct NovaRow: View {
    let icon: String
    let title: String
    var subtitle: String? = nil
    let action: () -> Void
    var body: some View {
        Button(action: action) {
            HStack(spacing: 12) {
                DaoIcon(name: icon).frame(width: 38, height: 38).background(Nova.secondary, in: .rect(cornerRadius: 10))
                VStack(alignment: .leading, spacing: 4) {
                    Text(title).font(.subheadline).foregroundStyle(Nova.foreground).lineLimit(1)
                    if let subtitle { Text(subtitle).font(.caption).foregroundStyle(Nova.muted).lineLimit(1) }
                }
                Spacer(minLength: 0)
                DaoIcon(name: "chevron-right", size: 16).foregroundStyle(Nova.muted)
            }.padding(.horizontal, 16).padding(.vertical, 11).contentShape(Rectangle())
        }.buttonStyle(.plain)
    }
}

struct UtilityPage<Content: View>: View {
    let title: String
    let back: () -> Void
    @ViewBuilder var content: Content
    var body: some View {
        VStack(spacing: 0) {
            HStack {
                IconButton(icon: "arrow-left", label: "back", action: back)
                Text(L(title)).font(.title3.weight(.semibold))
                Spacer()
            }.padding(.horizontal, 8).padding(.bottom, 8)
            content
        }.background(Nova.background)
    }
}
