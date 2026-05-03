import type { AnchorHTMLAttributes } from 'react';
import styles from './Button.module.css';

type Variant = 'primary' | 'ghost';

export interface ButtonProps extends AnchorHTMLAttributes<HTMLAnchorElement> {
  variant?: Variant;
  fullWidth?: boolean;
  external?: boolean;
}

export function Button({
  variant = 'primary',
  fullWidth = false,
  external = false,
  className,
  children,
  ...rest
}: ButtonProps) {
  const cls = [styles.base, styles[variant], fullWidth && styles.fullWidth, className]
    .filter(Boolean)
    .join(' ');

  const externalProps = external
    ? { target: '_blank', rel: 'noopener noreferrer' }
    : {};

  return (
    <a className={cls} {...externalProps} {...rest}>
      {children}
    </a>
  );
}
