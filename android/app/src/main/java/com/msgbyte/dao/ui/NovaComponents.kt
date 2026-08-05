package com.msgbyte.dao.ui

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.SwitchDefaults
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.composables.icons.lucide.ChevronLeft
import com.composables.icons.lucide.Lucide
import com.msgbyte.dao.R
import com.msgbyte.dao.ui.theme.LocalNovaColors

@Composable
fun NovaCard(
    modifier: Modifier = Modifier,
    content: @Composable () -> Unit,
) {
    val colors = LocalNovaColors.current
    Card(
        modifier = modifier,
        shape = RoundedCornerShape(16.dp),
        colors = CardDefaults.cardColors(containerColor = colors.surface),
        border = BorderStroke(1.dp, colors.border),
        elevation = CardDefaults.cardElevation(defaultElevation = 1.dp),
    ) {
        content()
    }
}

@Composable
fun LetterAvatar(
    letter: String,
    color: Color,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Box(
        modifier = modifier
            .size(38.dp)
            .background(color, RoundedCornerShape(10.dp)),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = letter,
            color = colors.onAccent,
            fontSize = if (letter.length > 2) 11.sp else 15.sp,
            fontWeight = FontWeight.SemiBold,
        )
    }
}

@Composable
fun ListRow(
    title: String,
    subtitle: String,
    modifier: Modifier = Modifier,
    letter: String? = null,
    avatarColor: Color = LocalNovaColors.current.accent,
    leadingIcon: ImageVector? = null,
    meta: String? = null,
    trailing: (@Composable () -> Unit)? = null,
    onClick: (() -> Unit)? = null,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier = modifier
            .fillMaxWidth()
            .then(if (onClick != null) Modifier.clickable(onClick = onClick) else Modifier)
            .padding(horizontal = 16.dp, vertical = 11.dp)
            .height(44.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        when {
            letter != null -> LetterAvatar(letter, avatarColor)
            leadingIcon != null -> Box(
                modifier = Modifier
                    .size(38.dp)
                    .background(colors.surfaceSecondary, RoundedCornerShape(10.dp))
                    .border(1.dp, colors.border, RoundedCornerShape(10.dp)),
                contentAlignment = Alignment.Center,
            ) {
                Icon(leadingIcon, contentDescription = null, modifier = Modifier.size(20.dp))
            }
        }
        if (letter != null || leadingIcon != null) Spacer(Modifier.width(14.dp))
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = title,
                color = colors.foreground,
                fontSize = 15.sp,
                fontWeight = FontWeight.Medium,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
            if (subtitle.isNotEmpty()) {
                Text(
                    text = subtitle,
                    color = colors.muted,
                    fontSize = 12.5.sp,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
            }
        }
        if (meta != null) {
            Text(meta, color = colors.faint, fontSize = 12.sp)
        }
        if (trailing != null) {
            Spacer(Modifier.width(8.dp))
            trailing()
        }
    }
}

@Composable
fun NovaSwitch(
    checked: Boolean,
    onCheckedChange: (Boolean) -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Switch(
        checked = checked,
        onCheckedChange = onCheckedChange,
        modifier = modifier,
        colors = SwitchDefaults.colors(
            checkedThumbColor = colors.surface,
            checkedTrackColor = colors.accent,
            uncheckedThumbColor = colors.surface,
            uncheckedTrackColor = colors.strongBorder,
            uncheckedBorderColor = Color.Transparent,
        ),
    )
}

@Composable
fun SegmentedControl(
    items: List<String>,
    selectedIndex: Int,
    onSelected: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier = modifier
            .fillMaxWidth()
            .background(colors.surfaceSecondary, RoundedCornerShape(11.dp))
            .border(1.dp, colors.border, RoundedCornerShape(11.dp))
            .padding(3.dp),
        horizontalArrangement = Arrangement.spacedBy(2.dp),
    ) {
        items.forEachIndexed { index, label ->
            Surface(
                modifier = Modifier
                    .weight(1f)
                    .height(36.dp)
                    .clickable { onSelected(index) },
                shape = RoundedCornerShape(8.dp),
                color = if (selectedIndex == index) colors.surface else Color.Transparent,
                shadowElevation = if (selectedIndex == index) 1.dp else 0.dp,
            ) {
                Box(contentAlignment = Alignment.Center) {
                    Text(
                        label,
                        color = if (selectedIndex == index) colors.foreground else colors.muted,
                        fontSize = 13.sp,
                        fontWeight = FontWeight.Medium,
                    )
                }
            }
        }
    }
}

@Composable
fun SectionLabel(text: String, modifier: Modifier = Modifier) {
    val colors = LocalNovaColors.current
    Text(
        text = text.uppercase(),
        modifier = modifier.padding(start = 6.dp, bottom = 8.dp, top = 14.dp),
        color = colors.muted,
        fontSize = 11.sp,
        letterSpacing = 1.3.sp,
    )
}

@Composable
fun ScreenHeader(
    title: String,
    onBack: () -> Unit,
    action: (@Composable () -> Unit)? = null,
) {
    val colors = LocalNovaColors.current
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .height(56.dp)
            .padding(horizontal = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        IconButton(onClick = onBack, modifier = Modifier.size(44.dp)) {
            Icon(
                Lucide.ChevronLeft,
                contentDescription = stringResource(R.string.navigate_back),
                tint = colors.foreground,
            )
        }
        Text(
            title,
            modifier = Modifier.weight(1f),
            color = colors.foreground,
            fontSize = 22.sp,
            fontWeight = FontWeight.Bold,
        )
        action?.invoke()
    }
}

@Composable
fun RowDivider(modifier: Modifier = Modifier) {
    val colors = LocalNovaColors.current
    HorizontalDivider(modifier = modifier, thickness = 1.dp, color = colors.border)
}
